#pragma once

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avassert.h>
#include <libavutil/audio_fifo.h>
#include "framework.h"

typedef struct StreamWriter {

    AVFormatContext* output_context;
    AVCodecContext* video_encoder;
    int video_stream_index;
    int64_t latest_video_pts;
    AVCodecContext* audio_encoder;
    int audio_stream_index;
    int64_t latest_audio_pts;

    const char* output;

} StreamWriter;

EXPORT StreamWriter* sw_allocate_writer(const char* output, const char* format);

EXPORT StreamWriter* sw_allocate_writer_from_format(const char* output, const AVOutputFormat* oformat);

EXPORT int sw_allocate_video_stream(StreamWriter* writer, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format);

EXPORT int sw_allocate_audio_stream(StreamWriter* writer, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt);

EXPORT int sw_write_frames(StreamWriter* writer, enum AVMediaType type, AVFrame* frames, int nb_frames);

EXPORT int sw_open_writer(StreamWriter* writer, AVDictionary** options);

EXPORT int sw_close_writer(StreamWriter* writer);

EXPORT int sw_free_writer(StreamWriter** writer);