#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>

#include "continuous-buffer.h"

int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt);

int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type);

int select_sample_rate(const AVCodec* codec);

int select_channel_layout(const AVCodec* codec);

int write_frame(AVFormatContext* fmt_ctx, AVCodecContext* c,
    AVStream* st, AVFrame* frame, AVPacket* pkt);

int get_stream_number(AVFormatContext* fmt_ctx, enum AVMediaType type);

AVFrame* copy_frame(AVFrame* src);

int convert_video_frame(AVFrame* src, AVFrame* dest);

int convert_audio_frame(AVFrame* src, AVFrame* dest);

int save_frame_to_file(AVFrame* frame, const char* filename, const char* codec_name);

AVDeviceInfoList* get_devices_list(const char* format);

int free_frames(AVFrame* frames, int64_t nb_frames);

AVCodecContext* allocate_video_codec_context(AVFormatContext* output, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format);

AVCodecContext* allocate_audio_codec_context(AVFormatContext* output, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt);