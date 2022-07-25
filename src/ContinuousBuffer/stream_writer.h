#pragma once

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

typedef struct StreamWriter {

    AVFormatContext* output_context;
    AVCodecContext* video_encoder;
    int video_stream_index;
    AVCodecContext* audio_encoder;
    int audio_stream_index;

    const char* output;

} StreamWriter;

StreamWriter* sw_allocate_writer(const char* output, const char* format);

int sw_allocate_video_stream(StreamWriter* writer, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format);

int sw_allocate_audio_stream(StreamWriter* writer, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt);

int sw_open_writer(StreamWriter* writer);

int sw_close_writer(StreamWriter** reader);