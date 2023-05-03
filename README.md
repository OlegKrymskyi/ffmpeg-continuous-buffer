# ffmpeg continuous buffer
## Overview
Application implements a continuous buffer muxer which could store the configurable amount of frames which then can be flushed to file.

Lets imagine that, you are trying to autogenerate highligts of a video game (for offline sport it would be the same). And for instance, you have powerfull AI which can recognize the goal during the football match. Then you can use this buffer to keep last 20 sec of a game in-memory and whenever you AI recognizer a spectacular moment, then it is fime to flush your buffer and pubish it on Youtube.

The idea of a buffer is more or less the same as OBS lib replay
```
obs-ffmpeg-mux.c
```

## How it works
Ffmpeg has defined AVOutputFormat structure which is responsible for the output stream format. Whenever, you want to create a video, you should define the video output format, it could be mp4 file, or RTMP stream, some in-memory storage or even do nothing (ffmpeg have null muxer which will just skip all the frames, they are using it for the test purposes mostly). So, overwall output format defines how to handle frames which your are trying to push to the output stream. 

This concept allows to create a custom output formats to extend Ffmpeg functionality as it was done in this library.

In this library you can find a muxer which implements an idea of a continous buffer.

Pay attention, that frame scaling rely on you and frame encoding (compression) rely on ffmpeg according to the encoding you chose. 

![Flow diagram](/docs/assets/img/flow.png)

## Prerequisites
1. Find a latest ffmpeg shared libraries build (--enable-shared). 
  You can find some build here for Windows: https://www.gyan.dev/ffmpeg/builds/
  For linux I would recomend to build it your own.
2. Download it and unpack to src/ffmpeg-shared folder.
```
docs/
src/
  ffmpeg-shared/
  ContinuousBuffer/
  continuous-buffer.sln
```

## Utils
In this lib source code you can also find a few helpers. 
```
stream-reader
stream-writer
```

Which could make frame handling process a bit simplier.

In the example, you can find a stream-reader which is capturing screen with 30fps (it might be even lower in real live)

```
    AVDictionary* gdigrab_opt = NULL;
    av_dict_set(&gdigrab_opt, "framerate", "30", 0);
    
    // Instanciate reader.
    desktopReader = sr_open_input("desktop", "gdigrab", &gdigrab_opt);
```

Then you can see that new video stream will be opened and it would be using a continuous buffer as an ouput format.
```
    bufferWriter = sw_allocate_writer_from_format(NULL, &continuous_buffer_muxer);

    AVRational time_base;
    time_base.num = 1;
    time_base.den = FPS;
    
    // Setup buffer format.
    sw_allocate_video_stream(
        bufferWriter,
        AV_CODEC_ID_H264, // Video encoder
        time_base,
        6400000,
        desktopReader->video_decoder->width,
        desktopReader->video_decoder->height,
        AV_PIX_FMT_YUV420P);

    // Setup buffer duration in ms.
    AVDictionary* cb_opt = cb_options(5000);
    sw_open_writer(bufferWriter, &cb_opt);
```

Besides there is an option to setup a callback for the reader to handle each arrived frame
```
int read_video_frame(AVFrame* frame, enum AVMediaType type, int64_t pts_time)
{
    if (sw_write_frames(bufferWriter, type, frame, 1) < 0)
    {
        return -1;
    }

    return 0;
}
sr_read_stream(desktopReader, read_video_frame);
```
