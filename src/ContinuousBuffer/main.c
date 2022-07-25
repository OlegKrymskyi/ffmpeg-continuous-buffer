#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "continuous-buffer.h"
#include "stream_reader.h"

StreamReader* reader;
ContinuousBuffer* buffer;
int videoFrameCounter = 0;

int read_frame(AVFrame* frame, enum AVMediaType type, int64_t pts_time)
{
    if (cb_push_frame(buffer, frame, type) < 0)
    {
        return -1;
    }

    if (type == AVMEDIA_TYPE_VIDEO)
    {
        videoFrameCounter++;
    }

    if (videoFrameCounter == 10 * 30)
    {
        cb_flush_to_file(buffer, "C:/temp/replay-buf.mp4", NULL);

        // Finish file reading and exit the program
        return -1;
    }

    return 0;
}

int main()
{
    reader = sr_open_desktop();

    float fps = sr_get_number_of_video_frames_per_second(reader);

    buffer = cb_allocate_buffer(10000);

    if (reader->video_decoder != NULL)
    {
        buffer->video = cb_allocate_stream_buffer_from_decoder(reader->input_context, reader->video_decoder, reader->video_stream_index, buffer->duration);
    }

    if (reader->audio_decoder != NULL)
    {
        buffer->audio = cb_allocate_stream_buffer_from_decoder(reader->input_context, reader->audio_decoder, reader->audio_stream_index, buffer->duration);
    }

    sr_read_stream(reader, read_frame);

    cb_free_buffer(&buffer);
    sr_free_reader(&reader);
}
