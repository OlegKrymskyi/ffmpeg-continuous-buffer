#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "continuous-buffer.h"

int videoFrameCounter = 0;
static int decode_packet(AVCodecContext* dec, const AVPacket* pkt, AVFrame* frame, ContinuousBuffer* buffer)
{
    int ret = 0;

    // submit the packet to the decoder
    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
        return ret;
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

        ret = cb_push_frame(buffer, frame, dec->codec->type);

        if (dec->codec->type == AVMEDIA_TYPE_VIDEO)
        {
            videoFrameCounter++;
        }

        if (videoFrameCounter == 60*10)
        {
            cb_flush_to_file(buffer, "C:/temp/replay-buf.mp4", NULL);
            return -1;
        }

        av_frame_unref(frame);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int main()
{
    const char* src_filename = "C:/temp/replay.mkv";

    AVFormatContext* inputFormat = NULL;
    /* open input file, and allocate format context */
    if (avformat_open_input(&inputFormat, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(inputFormat, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    ContinuousBuffer* buffer = cb_allocate_buffer_from_source(inputFormat, 20);

    int videoStreamIdx = -1;
    AVCodecContext* videoDecCtx = NULL;
    if (open_codec_context(&videoStreamIdx, &videoDecCtx, inputFormat, AVMEDIA_TYPE_VIDEO) < 0) {
        fprintf(stderr, "Could not open video stream\n");
        goto end;
    }

    int audioStreamIdx = -1;
    AVCodecContext* audioDecCtx = NULL;
    if (open_codec_context(&audioStreamIdx, &audioDecCtx, inputFormat, AVMEDIA_TYPE_AUDIO) < 0) {
        fprintf(stderr, "Could not open audio stream\n");
        goto end;
    }

    /* dump input information to stderr */
    av_dump_format(inputFormat, 0, src_filename, 0);

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        goto end;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate packet\n");
        goto end;
    }

    int ret = 0;
    /* read frames from the file */
    while (av_read_frame(inputFormat, pkt) >= 0) {
        // check if the packet belongs to a stream we are interested in, otherwise
        // skip it
        if (pkt->stream_index == videoStreamIdx)
            ret = decode_packet(videoDecCtx, pkt, frame, buffer);
        else if (pkt->stream_index == audioStreamIdx)
            ret = decode_packet(audioDecCtx, pkt, frame, buffer);
        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }

    /* flush the decoders */
    if (videoDecCtx)
        decode_packet(videoDecCtx, NULL, frame, buffer);
    if (audioDecCtx)
        decode_packet(audioDecCtx, NULL, frame, buffer);

    cb_flush_to_file(buffer, "C:/temp/replay-buf.jpg", NULL);

end:
    cb_free_buffer(&buffer);
    avcodec_free_context(&videoDecCtx);
    avcodec_free_context(&audioDecCtx);
    avformat_close_input(&inputFormat);
    av_packet_free(&pkt);
    av_frame_free(&frame);
}
