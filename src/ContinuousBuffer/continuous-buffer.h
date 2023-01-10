#pragma once

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/fifo.h>
#include <libavutil/audio_fifo.h>

#include "utils.h"

typedef struct ContinuousBufferStream {

    enum AVMediaType type;
    
    AVFifoBuffer* queue;
    AVRational time_base;

    enum AVCodecID codec;

    int64_t bit_rate;

    int width;
    int height;
    enum AVPixelFormat pixel_format;
    int nb_frames;

    int sample_rate;    
    int channel_layout;
    enum AVSampleFormat sample_fmt;
    int frame_size;

    int64_t duration;
} ContinuousBufferStream;

typedef struct ContinuousBuffer {

    int64_t duration;

    ContinuousBufferStream* video;
    ContinuousBufferStream* audio;

} ContinuousBuffer;

int cb_pop_all_packets_internal(AVFifoBuffer* queue, AVPacket** packets);

int cb_pop_all_packets(ContinuousBuffer* buffer, enum AVMediaType type, AVPacket** packets);

int cb_write_to_mp4(ContinuousBuffer* buffer, const char* output);

static int cb_init(AVFormatContext* avf);

static int cb_write_header(AVFormatContext* avf);

static int cb_write_trailer(AVFormatContext* avf);

static int cb_write_packet(AVFormatContext* avf, AVPacket* pkt);

static void cb_deinit(AVFormatContext* avf);

#define OFFSET(x) offsetof(ContinuousBuffer, x)
static const AVOption options[] = {

        {"duration", "Buffer duration", OFFSET(duration),
         AV_OPT_TYPE_INT64, {.i64 = 10000}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},

        {NULL},
};

static const AVClass continuous_buffer_muxer_class;

const AVOutputFormat continuous_buffer_muxer;