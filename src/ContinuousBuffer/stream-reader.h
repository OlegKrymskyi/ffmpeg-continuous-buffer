#pragma once

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

typedef struct StreamReader {

    AVFormatContext* input_context;
    AVCodecContext* video_decoder;
    int video_stream_index;
    AVCodecContext* audio_decoder;
    int audio_stream_index;

} StreamReader;

StreamReader* sr_open_stream_from_format(const char* input, AVInputFormat* format);

StreamReader* sr_open_stream(const char* input);

StreamReader* sr_open_input(const char* input, const char* format);

int sr_read_stream(StreamReader* reader, int (*callback)(AVFrame* frame, enum AVMediaType type, int64_t pts_time));

int sr_free_reader(StreamReader** reader);

float sr_get_number_of_video_frames_per_second(StreamReader* reader);