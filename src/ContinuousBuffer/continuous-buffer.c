#include "continuous-buffer.h"

int write_packet_callback(struct AVFormatContext* format, AVPacket* pkt)
{
    return 1;
}

ContinuousBuffer* cb_allocate_buffer(const char* format, int64_t maxDuration)
{
    ContinuousBuffer* buffer = av_mallocz(sizeof(ContinuousBuffer));

    buffer->duration = maxDuration;

    AVFormatContext* outputFormat = avformat_alloc_context();

    /* allocate the output media context */
    /*avformat_alloc_output_context2(&outputFormat, NULL, format, NULL);
    if (!outputFormat) {
        fprintf(stderr, "Could not decode output format from file extension: using FLV.\n");
        avformat_alloc_output_context2(&outputFormat, NULL, "flv", NULL);
    }

    if (!outputFormat) {
        fprintf(stderr, "Output format was not initialized.\n");
        return -1;
    }*/

    // Create internal Buffer for FFmpeg:
    const int iBufSize = 32 * 1024;
    void* pBuffer = av_mallocz(iBufSize);

    AVIOContext* pIOCtx = avio_alloc_context(pBuffer, iBufSize,  // internal Buffer and its size
        1,                  // bWriteable (1=true,0=false) 
        NULL,          // user data ; will be passed to our callback functions
        NULL,
        write_packet_callback,                  // Write callback function (not used in this example) 
        NULL);

    outputFormat->pb = pIOCtx;

    outputFormat->oformat = av_find_output_format(format);

    outputFormat->flags = AVFMT_FLAG_CUSTOM_IO;

    buffer->output = outputFormat;

    return buffer;
}

ContinuousBufferStream* cb_allocate_video_buffer(ContinuousBuffer* buffer, AVRational time_base, enum AVCodecID codecId, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format)
{
    ContinuousBufferStream* buffer_stream = av_mallocz(sizeof(ContinuousBufferStream));
    buffer_stream->type = AVMEDIA_TYPE_VIDEO;
    buffer_stream->codec = codecId;
    buffer_stream->width = width;
    buffer_stream->height = height;
    buffer_stream->pixel_format = pixel_format;
    buffer_stream->time_base = time_base;
    buffer_stream->bit_rate = bit_rate;

    buffer_stream->queue = av_fifo_alloc_array((size_t)time_base.den * buffer->duration / 1000, sizeof(AVPacket));

    AVCodecContext* c = NULL;

    /* find the encoder */
    AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) {
        fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codecId));
        return -1;
    }

    AVStream* st = avformat_new_stream(buffer->output, NULL);
    if (st == NULL) {
        fprintf(stderr, "Could not allocate stream\n");
        return -1;
    }

    st->id = buffer->output->nb_streams - 1;

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* put sample parameters */
    c->bit_rate = bit_rate;
    /* resolution must be a multiple of two */
    c->width = width;
    c->height = height;
    /* frames per second */
    c->time_base = time_base;
    c->framerate = (AVRational){ time_base.den, time_base.num };

    st->time_base = c->time_base;

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = pixel_format;

    if (codec == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    /* Some formats want stream headers to be separate. */
    if (buffer->output->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_parameters_from_context(st->codecpar, c);

    buffer_stream->encoder = c;

    buffer->video = buffer_stream;

    return buffer_stream;
}

ContinuousBufferStream* cb_allocate_audio_buffer(ContinuousBuffer* buffer, AVRational time_base, enum AVCodecID codecId, int sample_rate, int64_t bit_rate, int channel_layout,
    enum AVSampleFormat sample_fmt, int frame_size)
{
    // Set the default frame size if it was not determined. 
    // And the grow the queue during the buffering
    if (frame_size == 0)
    {
        if (channel_layout == 0)
        {
            channel_layout = av_get_default_channel_layout(2);
        }
        frame_size = sample_rate / av_get_channel_layout_nb_channels(channel_layout);
    }

    ContinuousBufferStream* buffer_stream = av_mallocz(sizeof(ContinuousBufferStream));
    buffer_stream->type = AVMEDIA_TYPE_AUDIO;
    buffer_stream->codec = codecId;
    buffer_stream->sample_rate = sample_rate;
    buffer_stream->bit_rate = bit_rate;
    buffer_stream->channel_layout = channel_layout;
    buffer_stream->time_base = time_base;
    buffer_stream->sample_fmt = sample_fmt;
    buffer_stream->frame_size = frame_size;

    size_t queue_length = (size_t)sample_rate * buffer->duration / (((size_t)frame_size) * 1000);
    buffer_stream->queue = av_fifo_alloc_array(queue_length, sizeof(AVPacket));

    AVCodecContext* c = NULL;

    /* find the encoder */
    AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) {
        fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codecId));
        return -1;
    }

    AVStream* st = avformat_new_stream(buffer->output, NULL);
    if (st == NULL) {
        fprintf(stderr, "Could not allocate stream\n");
        return -1;
    }

    st->id = buffer->output->nb_streams - 1;

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* put sample parameters */
    c->bit_rate = bit_rate;

    /* check that the encoder supports s16 pcm input */
    c->sample_fmt = sample_fmt;
    if (!check_sample_fmt(codec, c->sample_fmt)) {
        fprintf(stderr, "Encoder does not support sample format %s",
            av_get_sample_fmt_name(c->sample_fmt));
        exit(1);
    }

    /* select other audio parameters supported by the encoder */
    c->sample_rate = select_sample_rate(codec);
    c->channel_layout = channel_layout;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    /* Some formats want stream headers to be separate. */
    if (buffer->output->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    buffer_stream->encoder = c;

    st->time_base = (AVRational){ 1, c->sample_rate };

    avcodec_parameters_from_context(st->codecpar, c);

    buffer_stream->audio = av_audio_fifo_alloc(buffer_stream->encoder->sample_fmt, buffer_stream->encoder->channels, 1);

    buffer->audio = buffer_stream;    

    return buffer;
}

ContinuousBufferStream* cb_allocate_stream_buffer_from_decoder(ContinuousBuffer* buffer, AVFormatContext* inputFormat, AVCodecContext* decoder, int streamIndex)
{
    if (decoder->codec->type == AVMEDIA_TYPE_VIDEO)
    {
        AVRational time_base;
        time_base.den = inputFormat->streams[streamIndex]->avg_frame_rate.num;
        time_base.num = inputFormat->streams[streamIndex]->avg_frame_rate.den;
        return cb_allocate_video_buffer(
            buffer,
            time_base,
            decoder->codec_id,
            decoder->bit_rate,
            decoder->width,
            decoder->height,
            decoder->pix_fmt);
    }
    else if (decoder->codec->type == AVMEDIA_TYPE_AUDIO)
    {
        return cb_allocate_audio_buffer(
            buffer,
            inputFormat->streams[streamIndex]->time_base,
            decoder->codec_id,
            decoder->sample_rate,
            decoder->bit_rate,
            decoder->channel_layout,
            decoder->sample_fmt,
            decoder->frame_size);
    }

    return NULL;
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
        cb_allocate_stream_buffer_from_decoder(buffer, inputFormat, videoDecCtx, videoStreamIdx, duration);
        avcodec_free_context(&videoDecCtx);
    }

    int audioStreamIdx;
    AVCodecContext* audioDecCtx = NULL;
    if (open_codec_context(&audioStreamIdx, &audioDecCtx, inputFormat, AVMEDIA_TYPE_AUDIO) >= 0)
    {
        cb_allocate_stream_buffer_from_decoder(inputFormat, audioDecCtx, audioStreamIdx, duration);
        avcodec_free_context(&audioDecCtx);
    }

    return buffer;
}

int cb_free_buffer(ContinuousBuffer** buffer)
{
    ContinuousBuffer* b = *buffer;

    if (b->audio != NULL)
    {
        av_audio_fifo_free(b->audio->audio);
        av_fifo_free(b->audio->queue);
        av_free(b->audio);

        if (b->audio->encoder != NULL)
        {
            avcodec_free_context(&b->audio->encoder);
        }
    }

    if (b->video != NULL)
    {
        av_fifo_free(b->video->queue);
        av_free(b->video);

        if (b->video->encoder != NULL)
        {
            avcodec_free_context(&b->video->encoder);
        }
    }

    if (!(b->output->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&b->output->pb);

    /* free the stream */
    avformat_free_context(b->output);

    av_free(b);

    *buffer = NULL;
}

int cb_push_frame(ContinuousBuffer* buffer, AVFrame* frame, enum AVMediaType type)
{
    ContinuousBufferStream* buffer_stream = NULL;

    if (type == AVMEDIA_TYPE_VIDEO) {
        buffer_stream = buffer->video;
    }
    else if (type == AVMEDIA_TYPE_AUDIO) {
        buffer_stream = buffer->audio;
    }

    // Taking current amount the free space in the queue.
    int space = av_fifo_space(buffer_stream->queue);

    // Taking current amount of queue which was already used.
    int size = av_fifo_size(buffer_stream->queue);

    int64_t duration = cb_get_buffer_stream_duration(buffer);

    while (av_fifo_space(buffer_stream->queue) <= 0 || duration >= buffer->duration)
    {
        AVPacket* removePkt = av_mallocz(sizeof(AVPacket));
        av_fifo_generic_read(buffer_stream->queue, removePkt, sizeof(AVPacket), NULL);

        if (type == AVMEDIA_TYPE_AUDIO)
        {
            buffer_stream->nb_samples -= frame->nb_samples;
        }

        av_frame_free(&removePkt);

        duration = cb_get_buffer_stream_duration(buffer);
    }

    AVFrame* cloneFrame = copy_frame(frame);
    if (type == AVMEDIA_TYPE_VIDEO)
    {
        cb_push_video_frame_to_queue(buffer, cloneFrame);
    }
    /* if (av_fifo_generic_write(buffer->queue, cloneFrame, sizeof(AVPacket), NULL) > 0)
     {
         buffer->nb_samples += frame->nb_samples;
     }*/

    return 0;

    return -1;
}

int64_t cb_get_buffer_stream_duration(ContinuousBufferStream* buffer)
{
    double duration = 0;
    if (buffer->type == AVMEDIA_TYPE_VIDEO)
    {
        duration = buffer->nb_frames * buffer->time_base.num / (double)buffer->time_base.den;
    }
    else if (buffer->type == AVMEDIA_TYPE_AUDIO)
    {
        duration = buffer->nb_samples * (int64_t)buffer->time_base.num / (double)buffer->time_base.den;
    }

    return duration * 1000;
}

int cb_push_video_frame_to_queue(ContinuousBuffer* buffer, AVFrame* frame)
{
    AVPacket* pkt = av_packet_alloc();
    int stNum = get_stream_number(buffer->output, AVMEDIA_TYPE_VIDEO);

    AVFrame* tmp = av_frame_alloc();
    if (!tmp) {
        fprintf(stderr, "Could not allocate the frame\n");
        return -1;
    }

    int ret;

    static struct SwsContext* sws_ctx = NULL;
    sws_ctx = sws_getContext(frame->width, frame->height, frame->format,
        buffer->video->encoder->width, buffer->video->encoder->height, buffer->video->encoder->pix_fmt,
        SWS_BICUBIC, NULL, NULL, NULL);

    if (!sws_ctx) {
        fprintf(stderr, "sws_getContext was not initialized\n");
        return -1;
    }

    fprintf(stderr, "Frames %d\n", buffer->video->nb_frames);

    tmp->format = buffer->video->encoder->pix_fmt;
    tmp->width = buffer->video->encoder->width;
    tmp->height = buffer->video->encoder->height;
    tmp->pts = buffer->video->nb_frames;

    av_frame_get_buffer(tmp, 0);

    ret = sws_scale(sws_ctx,
        frame->data,
        frame->linesize,
        0,
        frame->height,
        tmp->data,
        tmp->linesize);
    if (ret < 0)
    {
        fprintf(stderr, "sws_scale error: %s\n", av_err2str(ret));
        return -1;
    }

    ret = write_frame(buffer->output, buffer->video->encoder, buffer->output->streams[stNum], tmp, pkt);
    if (ret < 0)
    {
        fprintf(stderr, "write_frame error: %s\n", av_err2str(ret));
        return -1;
    }

    buffer->video->nb_frames++;

    av_frame_free(&tmp);

    if (sws_ctx != NULL)
    {
        sws_freeContext(sws_ctx);
    }

    //av_packet_free(&pkt);
    av_fifo_generic_write(buffer->video->queue, pkt, sizeof(AVPacket), NULL);

    return 0;
}

static int64_t cb_pop_all_frames_internal(AVFifoBuffer* queue, AVPacket** packets)
{
    if (av_fifo_size(queue) == 0)
    {
        return 0;
    }

    int64_t nb_pkt = av_fifo_size(queue) / sizeof(AVPacket);

    packets = av_mallocz(sizeof(AVPacket) * nb_pkt);
    av_fifo_generic_read(queue, *packets, av_fifo_size(queue), NULL);

    return nb_pkt;
}

int64_t cb_pop_all_frames(ContinuousBuffer* buffer, enum AVMediaType type, AVPacket** packets)
{
    if (type == AVMEDIA_TYPE_VIDEO && buffer->video != NULL)
    {
        int64_t result = cb_pop_all_frames_internal(buffer->video->queue, packets);
        buffer->video->nb_frames = 0;
        return result;
    }
    else if (type == AVMEDIA_TYPE_AUDIO && buffer->audio != NULL)
    {
        int64_t result = cb_pop_all_frames_internal(buffer->audio->queue, packets);
        buffer->audio->nb_samples = 0;
        return result;
    }

    return -1;
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

        AVFrame* tmp = av_frame_alloc();
        if (!tmp) {
            fprintf(stderr, "Could not allocate video frame\n");
            return -1;
        }

        if (encoder->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            // Temporary fix,somehow,video files
            tmp->format = encoder->pix_fmt;
            tmp->width = frame->width;
            tmp->height = frame->height;
            tmp->pts = i;

            av_frame_get_buffer(tmp, 0);

            convert_video_frame(frame, tmp);

            write_frame(outputFormat, encoder, outputFormat->streams[stNum], tmp, pkt);
        }
        else
        {
            // Temporary fix,somehow,video files
            tmp->channels = encoder->channels;
            tmp->channel_layout = encoder->channel_layout;
            tmp->sample_rate = frame->sample_rate;
            tmp->format = frame->format;

            convert_audio_frame(frame, tmp);
            tmp->pts = nb_samples;
            write_frame(outputFormat, encoder, outputFormat->streams[stNum], tmp, pkt);
            nb_samples += tmp->nb_samples;
        }

        av_frame_free(&tmp);
        av_frame_free(&frame);
    }

    write_frame(outputFormat, encoder, outputFormat->streams[stNum], NULL, pkt);

    av_packet_free(&pkt);

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

int cb_start(ContinuousBuffer* buffer)
{
    av_dump_format(buffer->output, 0, NULL, 1);

    //int ret = avformat_write_header(buffer->output, NULL);

    /* Write the stream header, if any. */
    /*if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
        return -1;
    }*/

    //buffer->output->oformat->write_packet = write_packet_internal;

    return 0;
}

int cb_flush_to_file(const char* file)
{
    int64_t nb_frames;
    
    return 0;
}