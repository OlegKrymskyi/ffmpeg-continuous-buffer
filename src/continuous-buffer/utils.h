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

#include "framework.h"
#include "continuous-buffer.h"

EXPORT int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt);

EXPORT int open_codec_context(int* streamIndex, AVCodecContext** decCtx, AVFormatContext* inputFormat, enum AVMediaType type);

EXPORT int select_sample_rate(const AVCodec* codec);

EXPORT uint64_t select_channel_layout(const AVCodec* codec);

EXPORT int write_packet(AVFormatContext* fmt_ctx, AVCodecContext* c, AVStream* st, AVPacket* pkt);

EXPORT int write_frame(AVFormatContext* fmt_ctx, AVCodecContext* c,
    AVStream* st, AVFrame* frame, AVPacket* pkt);

EXPORT int get_stream_number(AVFormatContext* fmt_ctx, enum AVMediaType type);

EXPORT AVFrame* copy_frame(AVFrame* src);

EXPORT int convert_video_frame(AVFrame* src, AVFrame* dest);

EXPORT int convert_audio_frame(AVFrame* src, AVFrame* dest);

EXPORT int save_frame_to_file(AVFrame* frame, const char* filename, const char* codec_name);

EXPORT AVDeviceInfoList* get_devices_list(const char* format);

EXPORT int free_frames(AVFrame* frames, int64_t nb_frames);

EXPORT AVCodecContext* allocate_video_stream(AVFormatContext* avf, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format);

EXPORT AVCodecContext* allocate_audio_stream(AVFormatContext* avf, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt);