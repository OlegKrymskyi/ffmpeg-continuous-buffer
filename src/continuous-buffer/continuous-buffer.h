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

#pragma comment (lib, "avcodec.lib")
#pragma comment (lib, "avformat.lib")
#pragma comment (lib, "avdevice.lib")
#pragma comment (lib, "avfilter.lib")
#pragma comment (lib, "avutil.lib")
#pragma comment (lib, "postproc.lib")
#pragma comment (lib, "swresample.lib")
#pragma comment (lib, "swscale.lib")

#include "utils.h"
#include "framework.h"

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
    ContinuousBufferStream* video;
    ContinuousBufferStream* audio;

    int64_t duration;
} ContinuousBuffer;

EXPORT int cb_pop_all_packets_internal(AVFifoBuffer* queue, AVPacket** packets);

EXPORT int cb_pop_all_packets(ContinuousBuffer* buffer, enum AVMediaType type, AVPacket** packets);

EXPORT int cb_write_to_mp4(ContinuousBuffer* buffer, const char* output);

EXPORT AVDictionary* cb_options(int64_t duration);

static int cb_init(AVFormatContext* avf);

static int cb_write_packet(AVFormatContext* avf, AVPacket* pkt);

static void cb_deinit(AVFormatContext* avf);

#define OFFSET(x) offsetof(ContinuousBuffer, x)
static const AVOption options[] = {

        {"duration", "Buffer duration", OFFSET(duration),
         AV_OPT_TYPE_INT64, {.i64 = 10000}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
        
        {NULL},
};

EXPORT const AVClass continuous_buffer_muxer_class;

EXPORT const AVOutputFormat continuous_buffer_muxer;