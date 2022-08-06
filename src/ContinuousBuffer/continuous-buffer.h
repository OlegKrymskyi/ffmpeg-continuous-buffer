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

    int sample_rate;    
    int channel_layout;
    enum AVSampleFormat sample_fmt;
    int nb_samples;
    int frame_size;

} ContinuousBufferStream;

typedef struct ContinuousBuffer {

    ContinuousBufferStream* video;
    ContinuousBufferStream* audio;

    int64_t duration;

} ContinuousBuffer;

int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type);

ContinuousBufferStream* cb_allocate_video_buffer(ContinuousBuffer* buffer, AVRational time_base, enum AVCodecID codec, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format);

ContinuousBufferStream* cb_allocate_audio_buffer(ContinuousBuffer* buffer, AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout,
    enum AVSampleFormat sample_fmt, int frame_size);

ContinuousBuffer* cb_allocate_buffer_from_source(AVFormatContext* inputFormat, int64_t duration);

ContinuousBufferStream* cb_allocate_stream_buffer_from_decoder(ContinuousBuffer* buffer, AVFormatContext* inputFormat, AVCodecContext* decoder, int streamIndex);

int cb_free_buffer(ContinuousBuffer** buffer);

int cb_push_frame(ContinuousBuffer* buffer, AVFrame* frame, enum AVMediaType type);

int cb_push_frame_to_queue(ContinuousBufferStream* buffer, AVFrame* frame, int64_t maxDuration);

int cb_is_empty(ContinuousBuffer* buffer);

int64_t cb_pop_all_frames(ContinuousBuffer* buffer, enum AVMediaType type, AVFrame** frames);

ContinuousBuffer* cb_allocate_buffer(int64_t maxDuration);

int64_t cb_get_buffer_stream_duration(ContinuousBufferStream* buffer);