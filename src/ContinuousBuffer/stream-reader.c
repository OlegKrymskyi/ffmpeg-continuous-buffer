#include "stream-reader.h"
#include "utils.h"

static int decode_packet(AVCodecContext* dec, const AVPacket* pkt, AVFrame* frame, int (*callback)(AVFrame* frame, enum AVMediaType type, int64_t pts_time))
{
    int ret = 0;

    // submit the packet to the decoder
    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
        return ret;
    }

    AVRational time_base_q = { 1, AV_TIME_BASE };
    int64_t pts_time = 0;
    if (pkt != NULL)
    {
        pts_time = av_rescale_q(pkt->dts, dec->time_base, time_base_q);
    }

    // get all the available frames from the decoder
    while (ret >= 0) {

        ret = avcodec_receive_frame(dec, frame);
        if (ret < 0) {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
            return ret;
        }

        if (callback(frame, dec->codec->type, pts_time) < 0)
        {
            return -1;
        }

        av_frame_unref(frame);
        if (ret < 0)
            return ret;        
    }

    return 0;
}

StreamReader* sr_open_stream_from_format(const char* input, AVInputFormat* format)
{
    StreamReader* reader = av_mallocz(sizeof(StreamReader));

    AVFormatContext* inputFormat = NULL;
    /* open input file, and allocate format context */

    AVDictionary* opts = NULL;
    av_dict_set(&opts, "framerate", "60", 0);

    if (avformat_open_input(&inputFormat, input, format, &opts) < 0) {
        fprintf(stderr, "Could not find %s\n", input);
        av_free(inputFormat);
        return NULL;
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(inputFormat, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        av_free(inputFormat);
        return NULL;
    }

    reader->input_context = inputFormat;

    int videoStreamIdx = -1;
    AVCodecContext* videoDecCtx = NULL;
    if (open_codec_context(&videoStreamIdx, &videoDecCtx, inputFormat, AVMEDIA_TYPE_VIDEO) == 0) {
        reader->video_decoder = videoDecCtx;
    }
    reader->video_stream_index = videoStreamIdx;

    int audioStreamIdx = -1;
    AVCodecContext* audioDecCtx = NULL;
    if (open_codec_context(&audioStreamIdx, &audioDecCtx, inputFormat, AVMEDIA_TYPE_AUDIO) == 0) {
        reader->audio_decoder = audioDecCtx;
    }
    reader->audio_stream_index = audioStreamIdx;

    /* dump input information to stderr */
    av_dump_format(reader->input_context, 0, input, 0);

    return reader;
}

StreamReader* sr_open_input(const char* input, const char* format)
{
    const AVInputFormat* iformat = av_find_input_format(format);
    if (iformat == NULL)
    {
        return NULL;
    }

    StreamReader* reader = sr_open_stream_from_format(input, iformat);

    return reader;
}

StreamReader* sr_open_stream(const char* input)
{
    StreamReader* reader = sr_open_stream_from_format(input, NULL);

    return reader;
}

int sr_read_stream(StreamReader* reader, int (*callback)(AVFrame* frame, enum AVMediaType type, int64_t pts_time))
{
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        return -1;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate packet\n");
        return -1;
    }

    clock_t begin = clock();
    int ret = 0;
    /* read frames from the file */
    while (av_read_frame(reader->input_context, pkt) >= 0) {

        // check if the packet belongs to a stream we are interested in, otherwise
        // skip it
        if (pkt->stream_index == reader->video_stream_index)
            ret = decode_packet(reader->video_decoder, pkt, frame, callback);
        else if (pkt->stream_index == reader->audio_stream_index)
            ret = decode_packet(reader->audio_decoder, pkt, frame, callback);
        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("Reading time %f\n", time_spent);

    av_packet_free(&pkt);
    if (ret == 0)
    {
        /* flush the decoders */
        if (reader->video_stream_index >= 0)
            decode_packet(reader->video_decoder, NULL, frame, callback);
        if (reader->audio_stream_index >= 0)
            decode_packet(reader->audio_decoder, NULL, frame, callback);
    }
    
    av_frame_free(&frame);
}

int sr_free_reader(StreamReader** reader)
{
    StreamReader* r = *reader;

    if (r->audio_decoder != NULL)
    {
        avcodec_free_context(&r->audio_decoder);
    }

    if (r->video_decoder != NULL)
    {
        avcodec_free_context(&r->video_decoder);
    }

    avformat_close_input(&r->input_context);

    av_freep(reader);

    return 0;
}

float sr_get_number_of_video_frames_per_second(StreamReader* reader)
{
    if (reader->video_decoder == NULL)
    {
        return -1;
    }

    AVStream* stream = reader->input_context->streams[reader->video_stream_index];

    return stream->avg_frame_rate.num / (float)stream->avg_frame_rate.den;
}