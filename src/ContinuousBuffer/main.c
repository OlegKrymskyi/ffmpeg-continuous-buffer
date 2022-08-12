#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "continuous-buffer.h"
#include "stream-reader.h"
#include "stream-writer.h"

#define OUTPUT_BIT_RATE 96000

StreamReader* speakerReader;
StreamWriter* audioWriter;
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
        // Finish file reading and exit the program
        return -1;
    }

    return 0;
}

int read_audio_frame(AVFrame* frame, enum AVMediaType type, int64_t pts_time)
{
    if (type != AVMEDIA_TYPE_AUDIO)
    {
        return 0;
    }

    if (cb_push_frame(buffer, frame, type) < 0)
    {
        return -1;
    }

    audioFrameCounter += frame->nb_samples;

    if (audioFrameCounter >= 44100*5)
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

    speakerReader = sr_open_input("audio=Stereo Mix (Realtek(R) Audio)", "dshow");
    audioWriter = sw_allocate_writer("C:/temp/speakers-buf.mp4", NULL);

    sw_allocate_audio_stream(audioWriter, 
        AV_CODEC_ID_AAC, 
        speakerReader->audio_decoder->bit_rate, 
        speakerReader->audio_decoder->sample_rate, 
        av_get_default_channel_layout(2), 
        AV_SAMPLE_FMT_FLTP);

    sw_open_writer(audioWriter);

    buffer = cb_allocate_buffer(5000);

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
    sw_free_writer(&audioWriter);
}
