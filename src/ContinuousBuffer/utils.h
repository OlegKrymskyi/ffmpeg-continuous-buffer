#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>

#include "continuous-buffer.h"

int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt);

int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type);

int select_sample_rate(const AVCodec* codec);

int write_frame(AVFormatContext* fmt_ctx, AVCodecContext* c,
    AVStream* st, AVFrame* frame, AVPacket* pkt);

int get_stream_number(AVFormatContext* fmt_ctx, enum AVMediaType type);

AVFrame* copy_frame(AVFrame* src);

int convert_video_frame(AVFrame* src, AVFrame* dest);

int save_frame_to_file(AVFrame* frame, const char* filename, const char* codec_name);