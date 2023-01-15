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

    if (videoFrameCounter == 10 * FPS)
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

    desktopReader = sr_open_input("desktop", "gdigrab");
    bufferWriter = sw_allocate_writer_from_format(NULL, &continuous_buffer_muxer);

    AVRational time_base;
    time_base.num = 1;
    time_base.den = FPS;
    sw_allocate_video_stream(
        bufferWriter,
        AV_CODEC_ID_H264,
        time_base,
        6400000,
        desktopReader->video_decoder->width,
        desktopReader->video_decoder->height,
        AV_PIX_FMT_YUV420P);

    AVDictionary* opt = cb_options(7);
    sw_open_writer(bufferWriter, &opt);

    clock_t begin = clock();

    sr_read_stream(desktopReader, read_video_frame);

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("Reading time %f\n", time_spent);

    cb_write_to_mp4(bufferWriter->output_context->priv_data, "c:\\temp\\test-buff.mp4");

    sw_close_writer(bufferWriter);
    sr_free_reader(&desktopReader);
    sw_free_writer(&bufferWriter);
}
