#include "stream_writer.h"
#include "utils.h"

StreamWriter* sw_allocate_writer(const char* output, const char* format)
{    
    AVFormatContext* outputFormat;
    AVCodec* audioCodec, * videoCodec;
    AVCodecContext* audioCodecCtx, * videoCodecCtx;

    /* allocate the output media context */
    avformat_alloc_output_context2(&outputFormat, NULL, format, output);
    if (!outputFormat) {
        fprintf(stderr, "Could not decode output format from file extension: using FLV.\n");
        avformat_alloc_output_context2(&outputFormat, NULL, "flv", output);
    }
    if (!outputFormat)
    {
        fprintf(stderr, "Output format was not initialized.\n");
        return -1;
    }

    StreamWriter* writer = av_mallocz(sizeof(StreamWriter));

    writer->output = output;
    writer->output_context = outputFormat;

    return writer;
}

int sw_close_writer(StreamWriter** writer)
{
    StreamWriter* w = *writer;

    if (w->audio_encoder != NULL)
    {
        AVPacket* pkt = av_packet_alloc();
        write_frame(w->output_context, w->audio_encoder, w->audio_stream_index, NULL, pkt);
        av_packet_free(&pkt);
    }

    if (w->video_encoder != NULL)
    {
        AVPacket* pkt = av_packet_alloc();
        write_frame(w->output_context, w->video_encoder, w->video_stream_index, NULL, pkt);
        av_packet_free(&pkt);
    }

    av_write_trailer(w->output_context);

    if (w->audio_encoder != NULL)
    {
        avcodec_free_context(&w->audio_encoder);
    }

    if (w->video_encoder != NULL)
    {
        avcodec_free_context(&w->video_encoder);
    }

    if (!(w->output_context->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&w->output_context->pb);

    /* free the stream */
    avformat_free_context(w->output_context);

    av_free(w);

    *writer = NULL;
}

int sw_allocate_video_stream(StreamWriter* writer, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format)
{
    AVCodecContext* c = NULL;

    /* find the encoder */
    AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) {
        fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codecId));
        return -1;
    }

    AVStream* st = avformat_new_stream(writer->output, NULL);
    if (st == NULL) {
        fprintf(stderr, "Could not allocate stream\n");
        return -1;
    }

    st->id = writer->output_context->nb_streams - 1;

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
    if (writer->output_context->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_parameters_from_context(st->codecpar, c);

    writer->video_encoder = c;

    return 0;
}

int sw_allocate_audio_stream(StreamWriter* writer, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt)
{
    AVCodecContext* c = NULL;

    /* find the encoder */
    AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) {
        fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codecId));
        return -1;
    }

    AVStream* st = avformat_new_stream(writer->output_context, NULL);
    if (st == NULL) {
        fprintf(stderr, "Could not allocate stream\n");
        return -1;
    }

    st->id = writer->output_context->nb_streams - 1;

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
    if (writer->output_context->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    writer->audio_encoder = c;

    st->time_base = (AVRational){ 1, c->sample_rate };

    avcodec_parameters_from_context(st->codecpar, c);

    return 0;
}

int sw_open_writer(StreamWriter* writer)
{
    av_dump_format(writer->output_context, 0, writer->output, 1);

    int ret = 0;
    /* open the output file, if needed */
    if (!(writer->output_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(writer->output_context->pb), writer->output, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", writer->output,
                av_err2str(ret));
            return -1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(writer->output_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
        return -1;
    }

    return ret;
}

int sw_write_frames(StreamWriter* writer, enum AVMediaType type, AVFrame* frames, int nb_frames)
{
    AVPacket* pkt = av_packet_alloc();
    int stNum = get_stream_number(writer->output_context, type);

    AVFrame* frame = frames;

    static struct SwsContext* sws_ctx = NULL;
    if (type == AVMEDIA_TYPE_VIDEO)
    {
        sws_ctx = sws_getContext(frame->width, frame->height, frame->format,
            writer->video_encoder->width, writer->video_encoder->height, writer->video_encoder->pix_fmt,
            SWS_BICUBIC, NULL, NULL, NULL);

        if (!sws_ctx) {
            return -1;
        }
    }    

    for (int i = 0; i < nb_frames; i++)
    {
        if (type == AVMEDIA_TYPE_VIDEO)
        {
            AVFrame* tmp = av_frame_alloc();
            if (!tmp) {
                fprintf(stderr, "Could not allocate video frame\n");
                return -1;
            }
            tmp->format = writer->video_encoder->pix_fmt;
            tmp->width = writer->video_encoder->width;
            tmp->height = writer->video_encoder->height;
            tmp->pts = writer->latest_video_pts;

            av_frame_get_buffer(tmp, 0);

            sws_scale(sws_ctx,
                frame->data,
                frame->linesize,
                0,
                frame->height,
                tmp->data,
                tmp->linesize);

            write_frame(writer->output_context, writer->video_encoder, writer->output_context->streams[stNum], tmp, pkt);

            av_frame_free(&tmp);

            writer->latest_video_pts += 1;
        }
        else
        {
            frame->pts = writer->latest_audio_pts;
            write_frame(writer->output_context, writer->audio_encoder, writer->output_context->streams[stNum], frame, pkt);
            writer->latest_audio_pts += frame->nb_samples;
        }

        frame += (int)sizeof(AVFrame);
    }

    if (sws_ctx != NULL)
    {
        sws_freeContext(sws_ctx);
    }

    return 0;
}