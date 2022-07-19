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

typedef struct ContinuousBufferVideo {
    AVFifoBuffer* queue;
    AVRational time_base;

    enum AVCodecID codec;
    int width;
    int height;
    enum AVPixelFormat pixel_format;

} ContinuousBufferVideo;

typedef struct ContinuousBufferAudio {

    AVFifoBuffer* queue;
    AVRational time_base;

    enum AVCodecID codec;
    int sample_rate;
    int64_t bit_rate;
    int channel_layout;

} ContinuousBufferAudio;

typedef struct ContinuousBuffer {

    ContinuousBufferVideo* video;
    ContinuousBufferAudio* audio;

    int64_t duration;

} ContinuousBuffer;

int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type);

ContinuousBufferVideo* cb_allocate_video_buffer(AVRational time_base, enum AVCodecID codec, int width, int height, enum AVPixelFormat pixel_format);

ContinuousBufferAudio* cb_allocate_audio_buffer(AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout);

ContinuousBuffer* cb_allocate_buffer_from_source(AVFormatContext* inputFormat, int64_t duration);

int cb_free_buffer(ContinuousBuffer** buffer);

int cb_push_frame(ContinuousBuffer* buffer, AVFrame* frame, enum AVMediaType type);

int cb_push_frame_to_queue(AVFifoBuffer* queue, AVFrame* frame);