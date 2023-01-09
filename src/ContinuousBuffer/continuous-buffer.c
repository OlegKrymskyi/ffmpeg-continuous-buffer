#include "continuous-buffer.h"

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

static int cb_write_header(AVFormatContext* avf)
{
    return 0;
}

static int cb_write_trailer(AVFormatContext* avf)
{
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

static void cb_deinit(AVFormatContext* avf)
{
    ContinuousBuffer* b = avf->priv_data;

    if (b->audio != NULL)
    {
        av_fifo_free(b->audio->queue);
        av_free(b->audio);
    }

    if (b->video != NULL)
    {
        av_fifo_free(b->video->queue);
        av_free(b->video);
    }
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
    .write_header = cb_write_header,
    .write_packet = cb_write_packet,
    .write_trailer = cb_write_trailer,
    .deinit = cb_deinit,
    .priv_class = &continuous_buffer_muxer_class,
    .flags = AVFMT_NOFILE,
};