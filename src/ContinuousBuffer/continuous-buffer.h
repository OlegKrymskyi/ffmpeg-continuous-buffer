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

typedef struct BufferVideoStream {    
    AVFifoBuffer* queue;
    AVRational time_base;

    enum AVCodecID codec;
    int width;
    int height;
    enum AVPixelFormat pixel_format;

} BufferVideoStream;

typedef struct BufferAudioStream {

    AVFifoBuffer* queue;
    AVRational time_base;

    enum AVCodecID codec;
    int sample_rate;
    int64_t bit_rate;
    int channel_layout;

} BufferAudioStream;

typedef struct BufferStream {

    BufferVideoStream* video;
    BufferAudioStream* audio;

} BufferStream;

static int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type);

BufferVideoStream* cb_allocate_video_buffer(AVRational time_base, enum AVCodecID codec, int width, int height, enum AVPixelFormat pixel_format);

BufferAudioStream* cb_allocate_audio_buffer(AVRational time_base, enum AVCodecID codec, int sample_rate, int64_t bit_rate, int channel_layout);

BufferStream* cb_allocate_buffer_from_source(AVFormatContext* inputFormat);

int cb_free_buffer(BufferStream** buffer);