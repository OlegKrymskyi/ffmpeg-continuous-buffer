#include "utils.h"

int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat* p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type)
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

int select_sample_rate(const AVCodec* codec)
{
    const int* p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}

int write_frame(AVFormatContext* fmt_ctx, AVCodecContext* c,
    AVStream* st, AVFrame* frame, AVPacket* pkt)
{
    int ret;

    // send the frame to the encoder
    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
            av_err2str(ret));
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            exit(1);
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        pkt->stream_index = st->index;

        /* Write the compressed frame to the media file. */
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

int get_stream_number(AVFormatContext* fmt_ctx, enum AVMediaType type)
{
    for (int i = 0; i < fmt_ctx->nb_streams; i++)
    {
        if (fmt_ctx->streams[i]->codecpar->codec_type == type)
        {
            return i;
        }
    }

    return -1;
}

AVFrame* copy_frame(AVFrame* src)
{
    AVFrame* dest = av_frame_alloc();
    dest->format = src->format;
    dest->width = src->width;
    dest->height = src->height;
    dest->channels = src->channels;
    dest->channel_layout = src->channel_layout;
    dest->nb_samples = src->nb_samples;
    dest->sample_rate = src->sample_rate;
    av_frame_get_buffer(dest, 0);
    av_frame_make_writable(dest);
    av_frame_copy(dest, src);
    av_frame_copy_props(dest, src);
    return dest;
}