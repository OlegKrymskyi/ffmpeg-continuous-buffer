#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "continuous-buffer.h"
#include "stream_reader.h"

StreamReader* desktopReader;
StreamReader* speakerReader;
ContinuousBuffer* buffer;
int videoFrameCounter = 0;
int audioFrameCounter = 0;

int read_video_frame(AVFrame* frame, enum AVMediaType type, int64_t pts_time)
{
    if (cb_push_frame(buffer, frame, type) < 0)
    {
        return -1;
    }

    if (type == AVMEDIA_TYPE_VIDEO)
    {
        videoFrameCounter++;
    }

    if (videoFrameCounter == 15 * 30)
    {
        cb_flush_to_file(buffer, "C:/temp/desktop-buf.mp4", NULL);

        // Finish file reading and exit the program
        return -1;
    }

    return 0;
}

int read_audio_frame(AVFrame* frame, enum AVMediaType type, int64_t pts_time)
{
    if (cb_push_frame(buffer, frame, type) < 0)
    {
        return -1;
    }

    if (type == AVMEDIA_TYPE_AUDIO)
    {
        audioFrameCounter += frame->nb_samples;
    }

    if (audioFrameCounter == 1000)
    {
        cb_flush_to_file(buffer, "C:/temp/skeapers-buf.mp3", NULL);

        // Finish file reading and exit the program
        return -1;
    }

    return 0;
}

int main()
{
    avdevice_register_all();
    get_devices_list(AVMEDIA_TYPE_AUDIO);

    //desktopReader = sr_open_desktop();
    speakerReader = sr_open_speaker();

    buffer = cb_allocate_buffer(10000);

    /*if (desktopReader->video_decoder != NULL)
    {
        AVRational time_base;
        time_base.den = 30;
        time_base.num = 1;
        buffer->video = cb_allocate_video_buffer(time_base, AV_CODEC_ID_H264, 0, 
            desktopReader->video_decoder->width, desktopReader->video_decoder->height, AV_PIX_FMT_YUV420P, buffer->duration);
    }*/

    if (speakerReader->audio_decoder != NULL)
    {
        buffer->audio = cb_allocate_stream_buffer_from_decoder(speakerReader->input_context, speakerReader->audio_decoder, speakerReader->audio_stream_index, buffer->duration);
    }

    sr_read_stream(speakerReader, read_audio_frame);

    cb_free_buffer(&buffer);
    sr_free_reader(&desktopReader);
}
