#include "continuous-buffer.h"

ContinuousBufferVideo* cb_allocate_video_buffer(AVRational time_base, enum AVCodecID codec, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format, int64_t duration)
{
    ContinuousBufferVideo* buffer = av_mallocz(sizeof(*buffer));
    buffer->codec = codec;
    buffer->width = width;
    buffer->height = height;
    buffer->pixel_format = pixel_format;
    buffer->time_base = time_base;
    buffer->bit_rate = bit_rate;

    buffer->queue = av_fifo_alloc_array((size_t)time_base.den * duration, sizeof(AVFrame*));
    return buffer;
}

ContinuousBufferAudio* cb_allocate_audio_buffer(AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout, enum AVSampleFormat sample_fmt, int64_t duration)
{
    ContinuousBufferAudio* buffer = av_mallocz(sizeof(*buffer));
    buffer->codec = codec;
    buffer->sample_rate = sample_rate;
    buffer->bit_rate = bit_rate;
    buffer->channel_layout = channel_layout;
    buffer->time_base = time_base;
    buffer->sample_fmt = sample_fmt;

    buffer->queue = av_fifo_alloc_array((size_t)time_base.den * duration, sizeof(AVFrame*));

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
            inputFormat->video_codec_id, 
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
            inputFormat->video_codec_id, 
            audioDecCtx->sample_rate,
            audioDecCtx->bit_rate,
            audioDecCtx->channel_layout,
            audioDecCtx->sample_fmt,
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
    if (type == AVMEDIA_TYPE_VIDEO)
    {
        return cb_push_frame_to_queue(buffer->video->queue, frame);
    }
    else if (type == AVMEDIA_TYPE_AUDIO)
    {
        return cb_push_frame_to_queue(buffer->audio->queue, frame);
    }

    return -1;
}

int cb_push_frame_to_queue(AVFifoBuffer* queue, AVFrame* frame)
{
    if (av_fifo_space(queue) <= 0)
    {
        AVFrame* removedFrame = av_frame_alloc();
        av_fifo_generic_read(queue, frame, sizeof(frame), NULL);
        av_frame_free(&removedFrame);
    }

    return av_fifo_generic_write(queue, frame, sizeof(frame), NULL);
}

int cb_flush_to_file(ContinuousBuffer* buffer, const char* output, const char* format)
{
    AVFormatContext* outputFormat;
    AVOutputFormat* fmt;
    AVCodec* audioCodec, * videoCodec;
    AVCodecContext* audioCodecCtx, * videoCodecCtx;
    AVDictionary* opt = NULL;

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
    if (fmt->video_codec != AV_CODEC_ID_NONE && buffer->video != NULL) {
        cb_add_video_stream(buffer, fmt, &videoCodec, &videoCodecCtx);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE && buffer->audio != NULL) {
        cb_add_audio_stream(buffer->audio, fmt, &audioCodec, &audioCodecCtx);
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
    ret = avformat_write_header(outputFormat, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
    }

    av_write_trailer(outputFormat);

    avcodec_free_context(&videoCodecCtx);
    avcodec_free_context(&audioCodecCtx);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&outputFormat->pb);

    /* free the stream */
    avformat_free_context(outputFormat);

    return ret;
}

int cb_add_video_stream(
    ContinuousBuffer* buffer,
    AVOutputFormat* outputFormatContext,
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

    c = avcodec_alloc_context3(codec);
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
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    /* Some formats want stream headers to be separate. */
    if (outputFormatContext->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    *codecContext = c;

    return 0;
}

int cb_add_audio_stream(
    ContinuousBuffer* buffer,
    AVOutputFormat* outputFormatContext,
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

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    /* put sample parameters */
    c->bit_rate = buffer->audio->bit_rate;

    /* check that the encoder supports s16 pcm input */
    c->sample_fmt = buffer->audio->sample_fmt;
    if (!check_sample_fmt(codec, c->sample_fmt)) {
        fprintf(stderr, "Encoder does not support sample format %s",
            av_get_sample_fmt_name(c->sample_fmt));
        exit(1);
    }

    /* select other audio parameters supported by the encoder */
    c->sample_rate = select_sample_rate(codec);
    c->channel_layout = buffer->audio->channel_layout;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    /* Some formats want stream headers to be separate. */
    if (outputFormatContext->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    *codecContext = c;

    return 0;
}