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

int select_channel_layout(const AVCodec* codec)
{
    const int* p, * best_ch_layout;
    int best_nb_channels = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout = p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
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
    av_frame_copy(dest, src);
    return dest;
}

int convert_audio_frame(AVFrame* src, AVFrame* dest)
{
    int ret = 0;
    struct SwrContext* swr_ctx = swr_alloc();

    src->channel_layout = av_get_default_channel_layout(src->channels);

    av_opt_set_channel_layout(swr_ctx, "in_channel_layout", src->channel_layout, 0);
    av_opt_set_channel_layout(swr_ctx, "out_channel_layout", dest->channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", src->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", dest->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src->format, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dest->format, 0);
    ret = swr_init(swr_ctx);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n", av_err2str(ret));
        return ret;
    }

    ret = swr_config_frame(swr_ctx, dest, src);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n", av_err2str(ret));
        return ret;
    }

    ret = swr_convert_frame(swr_ctx, dest, src);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n", av_err2str(ret));
        return ret;
    }

    swr_free(&swr_ctx);

    return 0;
}

int convert_video_frame(AVFrame* src, AVFrame* dest)
{
    static struct SwsContext* sws_ctx;

    sws_ctx = sws_getContext(src->width, src->height, src->format,
        dest->width, dest->height, dest->format,
        SWS_BICUBIC, NULL, NULL, NULL);

    if (!sws_ctx) {
        return -1;
    }

    sws_scale(sws_ctx,
        src->data,
        src->linesize,
        0,
        src->height,
        dest->data,
        dest->linesize);

    sws_freeContext(sws_ctx);

    return 0;
}

void encode_frame_to_file(AVCodecContext* enc_ctx, AVFrame* frame, AVPacket* pkt, FILE* outfile)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int save_frame_to_file(AVFrame* frame, const char* filename, const char* codec_name)
{
    const AVCodec* codec;
    AVCodecContext* c = NULL;
    int ret;
    FILE* f;
    AVPacket* pkt;

    AVFrame* tmp = av_frame_alloc();
    if (!tmp) {
        fprintf(stderr, "Could not allocate video frame\n");
        return -1;
    }
    tmp->format = AV_PIX_FMT_BGR24;
    tmp->width = frame->width;
    tmp->height = frame->height;

    ret = av_frame_get_buffer(tmp, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        return -1;
    }

    convert_video_frame(frame, tmp);

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        return -1;
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    pkt = av_packet_alloc();
    if (!pkt)
        return -1;

    /* resolution must be a multiple of two */
    c->width = frame->width;
    c->height = frame->height;
    c->time_base = (AVRational){ 1, 1 };
    c->pix_fmt = tmp->format;

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        return -1;
    }

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        return -1;
    }

    /* flush the encoder */
    encode_frame_to_file(c, tmp, pkt, f);

    fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&tmp);
    av_packet_free(&pkt);
}

AVDeviceInfoList* get_devices_list(const char* format)
{
    AVDeviceInfoList* device_list = NULL;
    AVInputFormat* iformat = av_find_input_format(format);
    if (iformat == NULL)
    {
        return NULL;
    }

    avdevice_list_input_sources(iformat, NULL, NULL, &device_list);

    if (!device_list || device_list->nb_devices == 0)
    {
        avdevice_free_list_devices(&device_list);
    }

    return device_list;
}

int free_frames(AVFrame* frames, int64_t nb_frames)
{
    AVFrame* frame = NULL;
    for (int i = 0; i < nb_frames; i++)
    {
        frame = frames;
        frames++;
        //av_frame_free(&frame);
    }
}