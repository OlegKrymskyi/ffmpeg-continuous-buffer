#include "continuous-buffer.h"

ContinuousBufferStream* cb_allocate_video_buffer(AVRational time_base, enum AVCodecID codec, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format, int64_t duration)
{
    ContinuousBufferStream* buffer = av_mallocz(sizeof(*buffer));
    buffer->type = AVMEDIA_TYPE_VIDEO;
    buffer->codec = codec;
    buffer->width = width;
    buffer->height = height;
    buffer->pixel_format = pixel_format;
    buffer->time_base = time_base;
    buffer->bit_rate = bit_rate;

    buffer->queue = av_fifo_alloc_array((size_t)time_base.den * duration / 1000, sizeof(AVFrame));
    return buffer;
}

ContinuousBufferStream* cb_allocate_audio_buffer(AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout, 
    enum AVSampleFormat sample_fmt, int frame_size, int64_t duration)
{
    ContinuousBufferStream* buffer = av_mallocz(sizeof(*buffer));
    buffer->type = AVMEDIA_TYPE_AUDIO;
    buffer->codec = codec;
    buffer->sample_rate = sample_rate;
    buffer->bit_rate = bit_rate;
    buffer->channel_layout = channel_layout;
    buffer->time_base = time_base;
    buffer->sample_fmt = sample_fmt;

    buffer->queue = av_fifo_alloc_array((size_t)time_base.den * duration / ((size_t)frame_size * 1000), sizeof(AVFrame));

    return buffer;
}

ContinuousBuffer* cb_allocate_buffer_from_source(AVFormatContext* inputFormat, int64_t duration)
{
    ContinuousBuffer* buffer = av_mallocz(sizeof(*buffer));

    buffer->video = NULL;
    buffer->audio = NULL;
    buffer->duration = duration;

    int videoStreamIdx;
    AVCodecContext* videoDecCtx = NULL;
    if (open_codec_context(&videoStreamIdx, &videoDecCtx, inputFormat, AVMEDIA_TYPE_VIDEO) >= 0)
    {
        AVRational time_base;
        time_base.den = inputFormat->streams[videoStreamIdx]->avg_frame_rate.num;
        time_base.num = inputFormat->streams[videoStreamIdx]->avg_frame_rate.den;
        buffer->video = cb_allocate_video_buffer(
            time_base,
            videoDecCtx->codec_id,
            videoDecCtx->bit_rate,
            videoDecCtx->width, 
            videoDecCtx->height, 
            videoDecCtx->pix_fmt,
            duration);
        avcodec_free_context(&videoDecCtx);
    }

    int audioStreamIdx;
    AVCodecContext* audioDecCtx = NULL;
    if (open_codec_context(&audioStreamIdx, &audioDecCtx, inputFormat, AVMEDIA_TYPE_AUDIO) >= 0)
    {
        buffer->audio = cb_allocate_audio_buffer(
            inputFormat->streams[audioStreamIdx]->time_base,
            audioDecCtx->codec_id,
            audioDecCtx->sample_rate,
            audioDecCtx->bit_rate,
            audioDecCtx->channel_layout,
            audioDecCtx->sample_fmt,
            audioDecCtx->frame_size,
            duration);
        avcodec_free_context(&audioDecCtx);
    }

    return buffer;
}

int cb_free_buffer(ContinuousBuffer** buffer)
{
    ContinuousBuffer* b = *buffer;

    if (b->audio != NULL)
    {
        av_fifo_free(b->audio->queue);
        av_free(b->audio);
    }

    if (b->video != NULL)
    {
        av_fifo_free(b->video->queue);
        av_free(b->video);
    }

    *buffer = NULL;
}

int cb_push_frame(ContinuousBuffer* buffer, AVFrame* frame, enum AVMediaType type)
{
    if (type == AVMEDIA_TYPE_VIDEO && buffer->video != NULL)
    {
        return cb_push_frame_to_queue(buffer->video, frame, buffer->duration);
    }
    else if (type == AVMEDIA_TYPE_AUDIO && buffer->audio != NULL)
    {
        return cb_push_frame_to_queue(buffer->audio, frame, buffer->duration);
    }

    return -1;
}

int64_t cb_get_buffer_stream_duration(ContinuousBufferStream* buffer)
{
    double duration = av_q2d(buffer->time_base) * av_fifo_size(buffer->queue) / sizeof(AVFrame);
    if (buffer->type == AVMEDIA_TYPE_AUDIO)
    {
        duration = duration * buffer->nb_samples;
    }

    return duration * 1000;
}

int cb_push_frame_to_queue(ContinuousBufferStream* buffer, AVFrame* frame, int64_t maxDuration)
{
    // Taking current amount the free space in the queue.
    int space = av_fifo_space(buffer->queue);

    // Taking current amount of queue which was already used.
    int size = av_fifo_size(buffer->queue);

    int64_t duration = cb_get_buffer_stream_duration(buffer);

    while (av_fifo_space(buffer->queue) <= 0 || duration >= maxDuration)
    {
        AVFrame* removedFrame = av_mallocz(sizeof(AVFrame));
        av_fifo_generic_read(buffer->queue, removedFrame, sizeof(AVFrame), NULL);

        if (buffer->type == AVMEDIA_TYPE_AUDIO)
        {
            buffer->nb_samples -= removedFrame->nb_samples;
        }

        av_frame_free(&removedFrame);

        duration = cb_get_buffer_stream_duration(buffer);
    }

    AVFrame* cloneFrame = copy_frame(frame);
    int ret = av_fifo_generic_write(buffer->queue, cloneFrame, sizeof(AVFrame), NULL);
    if (ret == 0)
    {
        buffer->nb_samples += frame->nb_samples;
    }
    return ret;
}

int cb_flush_to_file(ContinuousBuffer* buffer, const char* output, const char* format)
{
    AVFormatContext* outputFormat;
    AVOutputFormat* fmt;
    AVCodec* audioCodec, * videoCodec;
    AVCodecContext* audioCodecCtx, * videoCodecCtx;

    /* allocate the output media context */
    avformat_alloc_output_context2(&outputFormat, NULL, format, output);
    if (!outputFormat) {
        fprintf(stderr, "Could not decode output format from file extension: using FLV.\n");
        avformat_alloc_output_context2(outputFormat, NULL, "flv", output);
    }
    if (!outputFormat)
    {
        fprintf(stderr, "Output format was not initialized.\n");
        return -1;
    }

    fmt = outputFormat->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (buffer->video != NULL) {
        cb_add_video_stream(buffer, outputFormat, &videoCodec, &videoCodecCtx);
    }
    if (buffer->audio != NULL) {
        cb_add_audio_stream(buffer, outputFormat, &audioCodec, &audioCodecCtx);
    }    

    av_dump_format(outputFormat, 0, output, 1);

    int ret = 0;
    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(outputFormat->pb), output, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", output,
                av_err2str(ret));
            return -1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(outputFormat, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
        return -1;
    }

    if (cb_is_empty(buffer) == 0)
    {
        if (buffer->video != NULL)
        {
            cb_write_queue(buffer->video->queue, outputFormat, videoCodecCtx);
        }

        if (buffer->audio != NULL)
        {
            cb_write_queue(buffer->audio->queue, outputFormat, audioCodecCtx);
        }

        av_write_trailer(outputFormat);
    }    

    avcodec_free_context(&videoCodecCtx);
    avcodec_free_context(&audioCodecCtx);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&outputFormat->pb);

    /* free the stream */
    avformat_free_context(outputFormat);

    return ret;
}

int cb_write_queue(AVFifoBuffer* queue, AVFormatContext* outputFormat, AVCodecContext* encoder)
{
    AVPacket* pkt = av_packet_alloc();
    int64_t frameCount = av_fifo_size(queue) / sizeof(AVFrame);
    int stNum = get_stream_number(outputFormat, encoder->codec_type);
    
    int nb_samples = 0;
    for (int i = 0; i < frameCount; i++)
    {
        AVFrame* frame = av_mallocz(sizeof(AVFrame));
        av_fifo_generic_read(queue, frame, sizeof(AVFrame), NULL);

        if (encoder->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            // Temporary fix,somehow,video files
            AVFrame* tmp = av_frame_alloc();
            if (!tmp) {
                fprintf(stderr, "Could not allocate video frame\n");
                return -1;
            }
            tmp->format = encoder->pix_fmt;
            tmp->width = frame->width;
            tmp->height = frame->height;
            tmp->pts = i;

            av_frame_get_buffer(tmp, 0);

            convert_video_frame(frame, tmp);

            write_frame(outputFormat, encoder, outputFormat->streams[stNum], tmp, pkt);
            
            av_frame_free(&tmp);
        }
        else
        {
            frame->pts = nb_samples;
            write_frame(outputFormat, encoder, outputFormat->streams[stNum], frame, pkt);
            nb_samples += frame->nb_samples;
        }

        av_frame_free(&frame);
    }

    write_frame(outputFormat, encoder, outputFormat->streams[stNum], NULL, pkt);

    return 0;
}

int cb_add_video_stream(
    ContinuousBuffer* buffer,
    AVFormatContext* outputFormat,
    AVCodec** codec,
    AVCodecContext** codecContext)
{
    AVCodecContext* c = NULL;

    /* find the encoder */
    *codec = avcodec_find_encoder(buffer->video->codec);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
            avcodec_get_name(buffer->video->codec));
        return -1;
    }

    AVStream* st = avformat_new_stream(outputFormat, NULL);
    if (st == NULL) {
        fprintf(stderr, "Could not allocate stream\n");
        return -1;
    }

    st->id = outputFormat->nb_streams - 1;

    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* put sample parameters */
    c->bit_rate = buffer->video->bit_rate;
    /* resolution must be a multiple of two */
    c->width = buffer->video->width;
    c->height = buffer->video->height;
    /* frames per second */
    c->time_base = buffer->video->time_base;
    c->framerate = (AVRational){ buffer->video->time_base.den, buffer->video->time_base.num };

    st->time_base = c->time_base;

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = buffer->video->pixel_format;

    if (buffer->video->codec == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
    if (avcodec_open2(c, *codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    /* Some formats want stream headers to be separate. */
    if (outputFormat->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_parameters_from_context(st->codecpar, c);

    *codecContext = c;

    return 0;
}

int cb_add_audio_stream(
    ContinuousBuffer* buffer,
    AVFormatContext* outputFormat,
    AVCodec** codec,
    AVCodecContext** codecContext)
{
    AVCodecContext* c = NULL;

    /* find the encoder */
    *codec = avcodec_find_encoder(buffer->audio->codec);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
            avcodec_get_name(buffer->audio->codec));
        return -1;
    }

    AVStream* st = avformat_new_stream(outputFormat, NULL);
    if (st == NULL) {
        fprintf(stderr, "Could not allocate stream\n");
        return -1;
    }

    st->id = outputFormat->nb_streams - 1;

    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* put sample parameters */
    c->bit_rate = buffer->audio->bit_rate;

    /* check that the encoder supports s16 pcm input */
    c->sample_fmt = buffer->audio->sample_fmt;
    if (!check_sample_fmt(*codec, c->sample_fmt)) {
        fprintf(stderr, "Encoder does not support sample format %s",
            av_get_sample_fmt_name(c->sample_fmt));
        exit(1);
    }

    /* select other audio parameters supported by the encoder */
    c->sample_rate = select_sample_rate(*codec);
    c->channel_layout = buffer->audio->channel_layout;

    /* open it */
    if (avcodec_open2(c, *codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    /* Some formats want stream headers to be separate. */
    if (outputFormat->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    *codecContext = c;

    st->time_base = (AVRational){ 1, c->sample_rate };

    avcodec_parameters_from_context(st->codecpar, c);

    return 0;
}

int cb_is_empty(ContinuousBuffer* buffer) 
{
    if (buffer->audio != NULL && av_fifo_size(buffer->audio->queue) > 0)
    {
        return 0;
    }

    if (buffer->video != NULL && av_fifo_size(buffer->video->queue) > 0)
    {
        return 0;
    }

    return 1;
}