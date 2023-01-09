#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "continuous-buffer.h"
#include "stream-reader.h"
#include "stream-writer.h"

#define FPS 30
#define OUTPUT_BIT_RATE 96000

StreamReader* desktopReader = NULL;
StreamWriter* bufferWriter = NULL;

int videoFrameCounter = 0;

int read_video_frame(AVFrame* frame, enum AVMediaType type, int64_t pts_time)
{
    if (sw_write_frames(bufferWriter, type, frame, 1) < 0)
    {
        return -1;
    }

    if (type == AVMEDIA_TYPE_VIDEO)
    {
        videoFrameCounter++;
    }

    if (videoFrameCounter == 5 * FPS)
    {
        // Finish file reading and exit the program
        return -1;
    }

    return 0;
}

int main()
{
    avdevice_register_all();
    get_devices_list("dshow");

    /*StreamReader* speakerReader = sr_open_input("audio=Stereo Mix (Realtek(R) Audio)", "dshow");
    StreamWriter* audioWriter = sw_allocate_writer("C:/temp/speakers-buf.mp4", NULL);

    sw_allocate_audio_stream(audioWriter,
        AV_CODEC_ID_AAC,
        speakerReader->audio_decoder->bit_rate,
        speakerReader->audio_decoder->sample_rate,
        av_get_default_channel_layout(2),
        AV_SAMPLE_FMT_FLTP);

    sw_open_writer(audioWriter);

    buffer = cb_allocate_buffer(20000);

    cb_allocate_audio_buffer(
        buffer,
        speakerReader->audio_decoder->time_base,
        speakerReader->audio_decoder->codec_id,
        speakerReader->audio_decoder->sample_rate,
        OUTPUT_BIT_RATE,
        av_get_default_channel_layout(2),
        speakerReader->audio_decoder->sample_fmt,
        speakerReader->audio_decoder->frame_size);

    sr_read_stream(speakerReader, read_audio_frame);

    cb_flush_to_writer(buffer, audioWriter);

    sw_close_writer(audioWriter);

    cb_free_buffer(&buffer);
    sr_free_reader(&speakerReader);
    sw_free_writer(&audioWriter);*/

    desktopReader = sr_open_input("desktop", "gdigrab");
    bufferWriter = sw_allocate_writer_from_format(NULL, &continuous_buffer_muxer);

    AVRational time_base;
    time_base.num = 1;
    time_base.den = FPS;
    sw_allocate_video_stream(
        bufferWriter,
        AV_CODEC_ID_H264,
        time_base,
        40000,
        desktopReader->video_decoder->width,
        desktopReader->video_decoder->height,
        AV_PIX_FMT_YUV420P);

    sw_open_writer(bufferWriter);

    clock_t begin = clock();

    sr_read_stream(desktopReader, read_video_frame);

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("Reading time %f\n", time_spent);

    sw_close_writer(bufferWriter);
    sr_free_reader(&desktopReader);
    sw_free_writer(&bufferWriter);
}
