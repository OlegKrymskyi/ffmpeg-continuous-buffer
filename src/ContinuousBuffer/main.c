#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "continuous-buffer.h"

int main()
{
    printf("Hello world");

    const char* src_filename = "C:/temp/game12-cut.mp4";

    AVFormatContext* inputFormat = NULL;
    /* open input file, and allocate format context */
    if (avformat_open_input(&inputFormat, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(inputFormat, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    BufferStream* buffer = cb_allocate_buffer_from_source(inputFormat);

    cb_free_buffer(&buffer);

    avformat_close_input(&inputFormat);
}
