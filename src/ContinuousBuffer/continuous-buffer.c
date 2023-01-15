#include "continuous-buffer.h"

int cb_pop_all_packets_internal(AVFifoBuffer* queue, AVPacket** packets)
{
    if (av_fifo_size(queue) == 0)
    {
        return 0;
    }

    int nb_pkt = av_fifo_size(queue) / sizeof(AVPacket);

    *packets = av_mallocz(sizeof(AVPacket) * nb_pkt);
    av_fifo_generic_read(queue, *packets, av_fifo_size(queue), NULL);

    return nb_pkt;
}

int cb_pop_all_packets_from_stream(ContinuousBufferStream* stream, AVPacket** packets)
{
    int result = cb_pop_all_packets_internal(stream->queue, packets);
    stream->duration = 0;
    return result;
}

int cb_pop_all_packets(ContinuousBuffer* buffer, enum AVMediaType type, AVPacket** packets)
{
    if (type == AVMEDIA_TYPE_VIDEO && buffer->video != NULL)
    {
        return cb_pop_all_packets_from_stream(buffer->video, packets);
    }
    else if (type == AVMEDIA_TYPE_AUDIO && buffer->audio != NULL)
    {
        return cb_pop_all_packets_from_stream(buffer->audio, packets);
    }

    return -1;
}

int cb_write_to_mp4(ContinuousBuffer* buffer, const char* output)
{
    AVFormatContext* outputFormat;

    /* allocate the output media context */
    avformat_alloc_output_context2(&outputFormat, NULL, "mp4", output);
    if (!outputFormat)
    {
        fprintf(stderr, "Output format was not initialized.\n");
        return -1;
    }

    AVCodecContext* video = NULL;
    if (buffer->video != NULL)
    {
        video = allocate_video_stream(outputFormat, buffer->video->codec, buffer->video->time_base, buffer->video->bit_rate, buffer->video->width, buffer->video->height, buffer->video->pixel_format);
    }

    AVCodecContext* audio = NULL;
    if (buffer->audio != NULL)
    {
        audio = allocate_audio_stream(outputFormat, buffer->audio->codec, buffer->audio->bit_rate, buffer->audio->sample_rate, buffer->audio->channel_layout, buffer->audio->sample_fmt);
    }

    av_dump_format(outputFormat, 0, output, 1);

    int ret = 0;
    /* open the output file, if needed */
    if (!(outputFormat->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(outputFormat->pb), output, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", output,
                av_err2str(ret));
            return -1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(outputFormat, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
        return -1;
    }

    int audio_idx = -1;
    int video_idx = -1;

    for (int i = 0; i < outputFormat->nb_streams; i++)
    {
        if (outputFormat->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) 
        {
            audio_idx = i;
        }

        if (outputFormat->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_idx = i;
        }
    }

    if (buffer->video != NULL)
    {
        AVPacket* packets = NULL;
        int nb_packets = cb_pop_all_packets(buffer, AVMEDIA_TYPE_VIDEO, &packets);
        
        for (int i = 0; i < nb_packets; i++) {
            write_packet(outputFormat, video, outputFormat->streams[video_idx], &packets[i]);
            av_packet_free(&packets[i]);
        }
    }

    if (buffer->audio != NULL)
    {
        AVPacket* packets = NULL;
        int nb_packets = cb_pop_all_packets(buffer, AVMEDIA_TYPE_AUDIO, &packets);

        for (int i = 0; i < nb_packets; i++) {
            write_packet(outputFormat, video, outputFormat->streams[audio_idx], &packets[i]);
            av_packet_free(&packets[i]);
        }
    }

    if (audio != NULL && audio_idx >= 0)
    {
        AVPacket* pkt = av_packet_alloc();
        write_frame(outputFormat, audio, outputFormat->streams[audio_idx], NULL, pkt);
        av_packet_free(&pkt);
    }

    if (video != NULL && video_idx >= 0)
    {
        AVPacket* pkt = av_packet_alloc();
        write_frame(outputFormat, video, outputFormat->streams[video_idx], NULL, pkt);
        av_packet_free(&pkt);
    }

    if (!(outputFormat->oformat->flags & AVFMT_NOFILE))
    {
        av_write_trailer(outputFormat);
    }

    if (audio != NULL)
    {
        avcodec_free_context(&audio);
    }

    if (video != NULL)
    {
        avcodec_free_context(&video);
    }

    if (!(outputFormat->oformat->flags & AVFMT_NOFILE))
    {
        /* Close the output file. */
        avio_closep(&outputFormat->pb);
    }

    /* free the stream */
    avformat_free_context(outputFormat);

    return ret;
}

AVDictionary* cb_options(int64_t duration)
{
    AVDictionary* opt = NULL;
    av_dict_set_int(&opt, "duration", duration, 0);

    return opt;
}

static int cb_is_empty(ContinuousBuffer* buffer) 
{
    if (buffer->audio != NULL && av_fifo_size(buffer->audio->queue) > 0)
    {
        return 0;
    }

    if (buffer->video != NULL && av_fifo_size(buffer->video->queue) > 0)
    {
        return 0;
    }

    return 1;
}

static int cb_init(AVFormatContext* avf)
{
    ContinuousBuffer* buffer = avf->priv_data;
    for (int i = 0; i < avf->nb_streams; i++)
    {
        if (avf->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            ContinuousBufferStream* buffer_stream = av_mallocz(sizeof(ContinuousBufferStream));
            buffer_stream->type = AVMEDIA_TYPE_VIDEO;
            buffer_stream->codec = avf->streams[i]->codecpar->codec_id;
            buffer_stream->width = avf->streams[i]->codecpar->width;
            buffer_stream->height = avf->streams[i]->codecpar->height;
            buffer_stream->pixel_format = avf->streams[i]->codecpar->format;
            buffer_stream->time_base = avf->streams[i]->time_base;
            buffer_stream->bit_rate = avf->streams[i]->codecpar->bit_rate;
            buffer_stream->duration = 0;

            buffer_stream->queue = av_fifo_alloc_array((size_t)avf->streams[i]->time_base.den * buffer->duration / 1000, sizeof(AVPacket));
            buffer->video = buffer_stream;
        }
        else if (avf->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            ContinuousBufferStream* buffer_stream = av_mallocz(sizeof(ContinuousBufferStream));
            buffer_stream->type = AVMEDIA_TYPE_AUDIO;
            buffer_stream->codec = avf->streams[i]->codecpar->codec_id;
            buffer_stream->sample_rate = avf->streams[i]->codecpar->sample_rate;
            buffer_stream->bit_rate = avf->streams[i]->codecpar->bit_rate;
            buffer_stream->channel_layout = avf->streams[i]->codecpar->channel_layout;
            buffer_stream->time_base = avf->streams[i]->time_base;
            buffer_stream->sample_fmt = avf->streams[i]->codecpar->format;
            buffer_stream->frame_size = avf->streams[i]->codecpar->frame_size;
            buffer_stream->duration = 0;

            size_t queue_length = (size_t)avf->streams[i]->codecpar->sample_rate * buffer->duration / (((size_t)avf->streams[i]->codecpar->frame_size) * 1000);
            buffer_stream->queue = av_fifo_alloc_array(queue_length, sizeof(AVPacket));

            buffer->audio = buffer_stream;
        }
    }

    return 0;
}

static int cb_write_packet(AVFormatContext* avf, AVPacket* pkt)
{
    if (pkt == NULL)
    {
        return 0;
    }

    AVPacket* clone = av_packet_clone(pkt);

    ContinuousBuffer* buffer = avf->priv_data;

    int s_idx = pkt->stream_index;

    ContinuousBufferStream* buffer_stream = NULL;
    int media_type = avf->streams[s_idx]->codecpar->codec_type;
    if (media_type == AVMEDIA_TYPE_VIDEO)
    {
        buffer_stream = buffer->video;
    }
    else if (media_type == AVMEDIA_TYPE_AUDIO)
    {
        buffer_stream = buffer->audio;
    }

    // Taking current amount the free space in the queue.
    int space = av_fifo_space(buffer_stream->queue);

    // Taking current amount of queue which was already used.
    int size = av_fifo_size(buffer_stream->queue);

    while (av_fifo_space(buffer_stream->queue) <= 0 || buffer_stream->duration >= buffer->duration)
    {
        AVPacket* removePkt = av_mallocz(sizeof(AVPacket));
        av_fifo_generic_read(buffer_stream->queue, removePkt, sizeof(AVPacket), NULL);

        buffer_stream->duration -= removePkt->duration;

        av_packet_free(&removePkt);
    }

    buffer_stream->duration += clone->duration;
    av_fifo_generic_write(buffer->video->queue, clone, sizeof(AVPacket), NULL);

    return 0;
}

static void cb_deinit_stream(ContinuousBufferStream* stream)
{
    AVPacket* packets = NULL;
    int nb_packets = cb_pop_all_packets_from_stream(stream, &packets);

    AVPacket* ppackets = packets;

    for (int i = 0; i < nb_packets; i++) {
        av_packet_unref(packets);
        av_packet_free_side_data(packets);
        packets++;
    }
    
    if (ppackets != NULL)
    {
        av_freep(&ppackets);
    }

    av_fifo_free(stream->queue);
    //av_freep(stream);
}

static void cb_deinit(AVFormatContext* avf)
{
    /*ContinuousBuffer* b = avf->priv_data;    

    if (b->audio != NULL)
    {
        cb_deinit_stream(b->audio);
    }

    if (b->video != NULL)
    {
        cb_deinit_stream(b->video);
    }*/
}

static const AVClass continuous_buffer_muxer_class = {
    .class_name = "Continuous buffer muxer",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

const AVOutputFormat continuous_buffer_muxer = {
    .name = "continuous-buffer",
    .long_name = "Continuous buffer",
    .priv_data_size = sizeof(ContinuousBuffer),
    .init = cb_init,
    .write_packet = cb_write_packet,
    .deinit = cb_deinit,
    .priv_class = &continuous_buffer_muxer_class,
    .flags = AVFMT_NOFILE,
};