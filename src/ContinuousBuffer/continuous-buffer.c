#include "continuous-buffer.h"

ContinuousBufferStream* cb_allocate_video_buffer(ContinuousBuffer* buffer, AVRational time_base, enum AVCodecID codec, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format)
{
    ContinuousBufferStream* buffer_stream = av_mallocz(sizeof(ContinuousBufferStream));
    buffer_stream->type = AVMEDIA_TYPE_VIDEO;
    buffer_stream->codec = codec;
    buffer_stream->width = width;
    buffer_stream->height = height;
    buffer_stream->pixel_format = pixel_format;
    buffer_stream->time_base = time_base;
    buffer_stream->bit_rate = bit_rate;

    buffer_stream->queue = av_fifo_alloc_array((size_t)time_base.den * buffer->duration / 1000, sizeof(AVFrame));
    return buffer;
}

ContinuousBufferStream* cb_allocate_audio_buffer(ContinuousBuffer* buffer, AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout,
    enum AVSampleFormat sample_fmt, int frame_size)
{
    // Set the default frame size if it was not determined. 
    // And the grow the queue during the buffering
    if (frame_size == 0)
    {
        frame_size = sample_rate / av_get_channel_layout_nb_channels(channel_layout);
    }

    ContinuousBufferStream* buffer_stream = av_mallocz(sizeof(ContinuousBufferStream));
    buffer_stream->type = AVMEDIA_TYPE_AUDIO;
    buffer_stream->codec = codec;
    buffer_stream->sample_rate = sample_rate;
    buffer_stream->bit_rate = bit_rate;
    buffer_stream->channel_layout = channel_layout;
    buffer_stream->time_base = time_base;
    buffer_stream->sample_fmt = sample_fmt;
    buffer_stream->frame_size = frame_size;

    size_t queue_length = (size_t)time_base.den * buffer->duration / ((size_t)frame_size * 1000);
    buffer_stream->queue = av_fifo_alloc_array(queue_length, sizeof(AVFrame));

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
        av_fifo_free(b->audio->queue);
        av_free(b->audio);
    }

    if (b->video != NULL)
    {
        av_fifo_free(b->video->queue);
        av_free(b->video);
    }

    av_free(b);

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
    double duration = 0;
    int64_t nb_frames = av_fifo_size(buffer->queue) / sizeof(AVFrame);
    if (buffer->type == AVMEDIA_TYPE_VIDEO)
    {
        duration = nb_frames * buffer->time_base.num / (double)buffer->time_base.den;
    }
    else if (buffer->type == AVMEDIA_TYPE_AUDIO)
    {
        duration = buffer->nb_samples * (int64_t)buffer->time_base.num / (double)buffer->time_base.den;
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
    if (av_fifo_generic_write(buffer->queue, cloneFrame, sizeof(AVFrame), NULL) > 0)
    {
        buffer->nb_samples += frame->nb_samples;
    }

    return 0;
}

static int64_t cb_pop_all_frames_internal(AVFifoBuffer* queue, AVFrame** frames)
{
    if (av_fifo_size(queue) == 0)
    {
        return 0;
    }

    int64_t nb_frames = av_fifo_size(queue) / sizeof(AVFrame);

    *frames = av_mallocz(sizeof(AVFrame) * nb_frames);
    av_fifo_generic_read(queue, *frames, av_fifo_size(queue), NULL);

    return nb_frames;
}

int64_t cb_pop_all_frames(ContinuousBuffer* buffer, enum AVMediaType type, AVFrame** frames)
{
    if (type == AVMEDIA_TYPE_VIDEO && buffer->video != NULL)
    {
        return cb_pop_all_frames_internal(buffer->video->queue, frames);
    }
    else if (type == AVMEDIA_TYPE_AUDIO && buffer->audio != NULL)
    {
        return cb_pop_all_frames_internal(buffer->audio->queue, frames);
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
        return -1;
    }

    /* select other audio parameters supported by the encoder */
    c->sample_rate = select_sample_rate(*codec);
    if (buffer->audio->channel_layout > 0)
    {        
        c->channel_layout = buffer->audio->channel_layout;
    }
    else 
    {
        c->channel_layout = select_channel_layout(*codec);
        c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
    }

    c->frame_size = buffer->audio->frame_size;

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

ContinuousBuffer* cb_allocate_buffer(int64_t maxDuration)
{
    ContinuousBuffer* buffer = av_mallocz(sizeof(ContinuousBuffer));

    buffer->duration = maxDuration;

    return buffer;
}