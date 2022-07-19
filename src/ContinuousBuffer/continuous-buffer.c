#include "continuous-buffer.h"

BufferVideoStream* cb_allocate_video_buffer(AVRational time_base, enum AVCodecID codec, int width, int height, enum AVPixelFormat pixel_format)
{
    BufferVideoStream* buffer = av_mallocz(sizeof(*buffer));
    buffer->codec = codec;
    buffer->width = width;
    buffer->height = height;
    buffer->pixel_format = pixel_format;
    buffer->time_base = time_base;

    // Initialize queue for around 20 secs.
    buffer->queue = av_fifo_alloc_array((size_t)time_base.den * 20, sizeof(AVFrame*));
    return buffer;
}

BufferAudioStream* cb_allocate_audio_buffer(AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout)
{
    BufferAudioStream* buffer = av_mallocz(sizeof(*buffer));
    buffer->codec = codec;
    buffer->sample_rate = sample_rate;
    buffer->bit_rate = bit_rate;
    buffer->channel_layout = channel_layout;
    buffer->time_base = time_base;

    // Initialize queue for around 20 secs.
    buffer->queue = av_fifo_alloc_array((size_t)time_base.den * 20, sizeof(AVFrame*));

    return buffer;
}

BufferStream* cb_allocate_buffer_from_source(AVFormatContext* inputFormat)
{
    BufferStream* buffer = av_mallocz(sizeof(*buffer));

    buffer->video = NULL;
    buffer->audio = NULL;

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
            videoDecCtx->width, 
            videoDecCtx->height, 
            videoDecCtx->pix_fmt);
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
            audioDecCtx->channel_layout);
        avcodec_free_context(&audioDecCtx);
    }

    return buffer;
}

int cb_free_buffer(BufferStream** buffer)
{
    BufferStream* b = *buffer;

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

static int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream* st;
    const AVCodec* dec = NULL;

    ret = av_find_best_stream(inputFormat, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input format context'\n",
            av_get_media_type_string(type));
        return ret;
    }
    else {
        stream_index = ret;
        st = inputFormat->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *decCtx = avcodec_alloc_context3(dec);
        if (!*decCtx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*decCtx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if ((ret = avcodec_open2(*decCtx, dec, NULL)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                av_get_media_type_string(type));
            return ret;
        }
        *streamIndex = stream_index;
    }

    return 0;
}

