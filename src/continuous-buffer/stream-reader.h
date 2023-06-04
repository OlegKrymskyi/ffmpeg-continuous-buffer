#pragma once

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "framework.h"

typedef struct StreamReader {

    AVFormatContext* input_context;
    AVCodecContext* video_decoder;
    int video_stream_index;
    AVCodecContext* audio_decoder;
    int audio_stream_index;

} StreamReader;

EXPORT StreamReader* sr_open_stream_from_format(const char* input, AVInputFormat* format, AVDictionary** opts);

EXPORT StreamReader* sr_open_stream(const char* input, AVDictionary** opts);

EXPORT StreamReader* sr_open_input(const char* input, const char* format, AVDictionary** opts);

EXPORT int sr_read_stream(StreamReader* reader, int (*callback)(AVFrame* frame, enum AVMediaType type, int64_t pts_time));

EXPORT int sr_free_reader(StreamReader** reader);

EXPORT float sr_get_number_of_video_frames_per_second(StreamReader* reader);