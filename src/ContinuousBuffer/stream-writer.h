#pragma once

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avassert.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/threadmessage.h>

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

typedef struct FifoContext {
    const AVClass* class;
    AVFormatContext* avf;

    char* format;
    AVDictionary* format_options;

    int queue_size;
    AVThreadMessageQueue* queue;

    void* writer_thread;

    /* Return value of last write_trailer_call */
    int write_trailer_ret;

    /* Time to wait before next recovery attempt
     * This can refer to the time in processed stream,
     * or real time. */
    int64_t recovery_wait_time;

    /* Maximal number of unsuccessful successive recovery attempts */
    int max_recovery_attempts;

    /* Whether to attempt recovery from failure */
    int attempt_recovery;

    /* If >0 stream time will be used when waiting
     * for the recovery attempt instead of real time */
    int recovery_wait_streamtime;

    /* If >0 recovery will be attempted regardless of error code
     * (except AVERROR_EXIT, so exit request is never ignored) */
    int recover_any_error;

    /* Whether to drop packets in case the queue is full. */
    int drop_pkts_on_overflow;

    /* Whether to wait for keyframe when recovering
     * from failure or queue overflow */
    int restart_with_keyframe;

    void* overflow_flag_lock;
    int overflow_flag_lock_initialized;
    /* Value > 0 signals queue overflow */
    volatile uint8_t overflow_flag;

    int64_t queue_duration;
    int64_t last_sent_dts;
    int64_t timeshift;
} FifoContext;

typedef struct FifoThreadContext {
    AVFormatContext* avf;

    /* Timestamp of last failure.
     * This is either pts in case stream time is used,
     * or microseconds as returned by av_getttime_relative() */
    int64_t last_recovery_ts;

    /* Number of current recovery process
     * Value > 0 means we are in recovery process */
    int recovery_nr;

    /* If > 0 all frames will be dropped until keyframe is received */
    uint8_t drop_until_keyframe;

    /* Value > 0 means that the previous write_header call was successful
     * so finalization by calling write_trailer and ff_io_close must be done
     * before exiting / reinitialization of underlying muxer */
    uint8_t header_written;

    int64_t last_received_dts;
} FifoThreadContext;

typedef enum FifoMessageType {
    FIFO_NOOP,
    FIFO_WRITE_HEADER,
    FIFO_WRITE_PACKET,
    FIFO_FLUSH_OUTPUT
} FifoMessageType;

typedef struct FifoMessage {
    FifoMessageType type;
    AVPacket pkt;
} FifoMessage;

StreamWriter* sw_allocate_writer(const char* output, const char* format);

int sw_allocate_video_stream(StreamWriter* writer, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format);

int sw_allocate_audio_stream(StreamWriter* writer, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt);

int sw_write_frames(StreamWriter* writer, enum AVMediaType type, AVFrame* frames, int nb_frames);

int sw_open_writer(StreamWriter* writer);

int sw_close_writer(StreamWriter* writer);

int sw_free_writer(StreamWriter** writer);

int sw_flush_to_file(StreamWriter* writer, const char* output, const char* format);