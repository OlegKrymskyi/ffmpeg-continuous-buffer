#include "stream-writer.h"
#include "utils.h"

StreamWriter* sw_allocate_writer(const char* output, const char* format)
{    
    AVFormatContext* outputFormat;

    AVOutputFormat* of = av_guess_format("fifo", NULL, NULL);
    /* allocate the output media context */
    avformat_alloc_output_context2(&outputFormat, of, NULL, NULL);
    if (!outputFormat)
    {
        fprintf(stderr, "Output format was not initialized.\n");
        return -1;
    }

    StreamWriter* writer = av_mallocz(sizeof(StreamWriter));

    writer->output = av_mallocz(strlen(output));
    strcpy(writer->output, output);

    writer->output_context = outputFormat;

    return writer;
}

int sw_close_writer(StreamWriter* writer)
{
    StreamWriter* w = writer;

    if (w->audio_encoder != NULL)
    {
        AVPacket* pkt = av_packet_alloc();
        write_frame(w->output_context, w->audio_encoder, w->output_context->streams[w->audio_stream_index], NULL, pkt);
        av_packet_free(&pkt);
    }

    if (w->video_encoder != NULL)
    {
        AVPacket* pkt = av_packet_alloc();
        write_frame(w->output_context, w->video_encoder, w->output_context->streams[w->video_stream_index], NULL, pkt);
        av_packet_free(&pkt);
    }

    av_write_trailer(w->output_context);

    if (w->audio_encoder != NULL)
    {
        avcodec_free_context(&w->audio_encoder);
    }

    if (w->video_encoder != NULL)
    {
        avcodec_free_context(&w->video_encoder);
    }

    if (!(w->output_context->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&w->output_context->pb);

    /* free the stream */
    avformat_free_context(w->output_context);
}

int sw_free_writer(StreamWriter** writer)
{
    av_free(*writer);

    *writer = NULL;
}

int sw_allocate_video_stream(StreamWriter* writer, enum AVCodecID codecId, AVRational time_base, int64_t bit_rate, int width, int height, enum AVPixelFormat pixel_format)
{
    AVCodecContext* c = allocate_video_codec_context(writer->output_context, codecId, time_base, bit_rate, width, height, pixel_format);
    if (c == NULL) {
        return -1;
    }

    writer->video_encoder = c;

    return 0;
}

int sw_allocate_audio_stream(StreamWriter* writer, enum AVCodecID codecId, int64_t bit_rate, int sample_rate, int channel_layout, enum AVSampleFormat sample_fmt)
{
    AVCodecContext* c = allocate_audio_codec_context(writer->output, codecId, bit_rate, sample_rate, channel_layout, sample_fmt);

    if (c == NULL) {
        return -1;
    }

    writer->audio_encoder = c;

    return 0;
}

int sw_open_writer(StreamWriter* writer)
{    
    av_dump_format(writer->output_context, 0, NULL, 1);

    int ret = 0;
    /* open the output file, if needed */
    /*if (!(writer->output_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(writer->output_context->pb), writer->output, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", writer->output,
                av_err2str(ret));
            return -1;
        }
    }*/

    AVDictionary* opt = NULL;

    av_dict_set(&opt, "fifo_format", "null", 0);
    av_dict_set_int(&opt, "drop_pkts_on_overflow", 1, 0);
    av_dict_set_int(&opt, "queue_size", 150, 0);

    /* Write the stream header, if any. */
    ret = avformat_write_header(writer->output_context, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
        return -1;
    }

    return ret;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 * @param[out] converted_input_samples Array of converted samples. The
 *                                     dimensions are reference, channel
 *                                     (for multi-channel audio), sample.
 * @param      output_codec_context    Codec context of the output file
 * @param      frame_size              Number of samples to be converted in
 *                                     each round
 * @return Error code (0 if successful)
 */
static int init_converted_samples(uint8_t*** converted_input_samples,
    AVCodecContext* output_codec_context,
    int frame_size)
{
    int error;

    /* Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*converted_input_samples = calloc(output_codec_context->channels,
        sizeof(**converted_input_samples)))) {
        fprintf(stderr, "Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }

    /* Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error = av_samples_alloc(*converted_input_samples, NULL,
        output_codec_context->channels,
        frame_size,
        output_codec_context->sample_fmt, 0)) < 0) {
        fprintf(stderr,
            "Could not allocate converted input samples (error '%s')\n",
            av_err2str(error));
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        return error;
    }
    return 0;
}

/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is
 * specified by frame_size.
 * @param      input_data       Samples to be decoded. The dimensions are
 *                              channel (for multi-channel audio), sample.
 * @param[out] converted_data   Converted samples. The dimensions are channel
 *                              (for multi-channel audio), sample.
 * @param      frame_size       Number of samples to be converted
 * @param      resample_context Resample context for the conversion
 * @return Error code (0 if successful)
 */
static int convert_samples(const uint8_t** input_data,
    uint8_t** converted_data, const int frame_size,
    SwrContext* resample_context)
{
    int error;

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resample_context,
        converted_data, frame_size,
        input_data, frame_size)) < 0) {
        fprintf(stderr, "Could not convert input samples (error '%s')\n",
            av_err2str(error));
        return error;
    }

    return 0;
}

/**
 * Add converted input audio samples to the FIFO buffer for later processing.
 * @param fifo                    Buffer to add the samples to
 * @param converted_input_samples Samples to be added. The dimensions are channel
 *                                (for multi-channel audio), sample.
 * @param frame_size              Number of samples to be converted
 * @return Error code (0 if successful)
 */
static int add_samples_to_fifo(AVAudioFifo* fifo,
    uint8_t** converted_input_samples,
    const int frame_size)
{
    int error;

    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }

    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void**)converted_input_samples,
        frame_size) < frame_size) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

static int sw_write_video_frames(StreamWriter* writer, AVFrame* frames, int nb_frames)
{
    for (int i = 0; i < nb_frames; i++)
    {
        AVPacket* pkt = av_packet_alloc();
        int stNum = get_stream_number(writer->output_context, AVMEDIA_TYPE_VIDEO);

        AVFrame* frame = frames;

        AVFrame* tmp = av_frame_alloc();
        if (!tmp) {
            fprintf(stderr, "Could not allocate the frame\n");
            return -1;
        }

        int ret;

        static struct SwsContext* sws_ctx = NULL;
        sws_ctx = sws_getContext(frame->width, frame->height, frame->format,
            writer->video_encoder->width, writer->video_encoder->height, writer->video_encoder->pix_fmt,
            SWS_BICUBIC, NULL, NULL, NULL);

        if (!sws_ctx) {
            fprintf(stderr, "sws_getContext was not initialized\n");
            return -1;
        }

        tmp->format = writer->video_encoder->pix_fmt;
        tmp->width = writer->video_encoder->width;
        tmp->height = writer->video_encoder->height;
        tmp->pts = writer->latest_video_pts;

        av_frame_get_buffer(tmp, 0);

        frame = frames;
        ret = sws_scale(sws_ctx,
            frame->data,
            frame->linesize,
            0,
            frame->height,
            tmp->data,
            tmp->linesize);
        if (ret < 0)
        {
            fprintf(stderr, "sws_scale error: %s\n", av_err2str(ret));
            return -1;
        }

        tmp->pts = writer->latest_video_pts;

        ret = write_frame(writer->output_context, writer->video_encoder, writer->output_context->streams[stNum], tmp, pkt);
        if (ret < 0)
        {
            fprintf(stderr, "write_frame error: %s\n", av_err2str(ret));
            return -1;
        }

        writer->latest_video_pts += 1;
        
        av_frame_free(&tmp);

        if (sws_ctx != NULL)
        {
            sws_freeContext(sws_ctx);
        }

        av_packet_free(&pkt);

        frames++;
    }

    return 0;
}

static int sw_write_audio_frames(StreamWriter* writer, AVFrame* frames, int nb_frames)
{
    AVPacket* pkt = av_packet_alloc();
    int stNum = get_stream_number(writer->output_context, AVMEDIA_TYPE_AUDIO);

    AVFrame* frame = frames;

    AVFrame* tmp = av_frame_alloc();
    if (!tmp) {
        fprintf(stderr, "Could not allocate the frame\n");
        return -1;
    }

    int ret;

    static struct SwrContext* swr_ctx = NULL;
    uint8_t** converted_input_samples = NULL;
    AVAudioFifo* fifo = NULL;

    /*
        * Perform a sanity check so that the number of converted samples is
        * not greater than the number of samples to be converted.
        * If the sample rates differ, this case has to be handled differently
        */
    av_assert0(frame->sample_rate == writer->audio_encoder->sample_rate);

    swr_ctx = swr_alloc();

    int src_channel_layout = frame->channel_layout;
    if (src_channel_layout == 0)
    {
        src_channel_layout = av_get_default_channel_layout(frame->channels);
    }
    av_opt_set_channel_layout(swr_ctx, "in_channel_layout", src_channel_layout, 0);
    av_opt_set_channel_layout(swr_ctx, "out_channel_layout", writer->audio_encoder->channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", frame->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", writer->audio_encoder->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", frame->format, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", writer->audio_encoder->sample_fmt, 0);
    ret = swr_init(swr_ctx);
    if (ret < 0) {
        fprintf(stderr, "swr_init error: %s\n", av_err2str(ret));
        return -1;
    }

    tmp->channels = writer->audio_encoder->channels;
    tmp->channel_layout = writer->audio_encoder->channel_layout;
    tmp->sample_rate = writer->audio_encoder->sample_rate;
    tmp->format = writer->audio_encoder->sample_fmt;
    tmp->nb_samples = writer->audio_encoder->frame_size;
    /* Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified. */
    if ((ret = av_frame_get_buffer(tmp, 0)) < 0) 
    {
        fprintf(stderr, "Could not allocate output frame samples (error '%s')\n", av_err2str(ret));
        av_frame_free(tmp);
        return -1;
    }

    fifo = av_audio_fifo_alloc(writer->audio_encoder->sample_fmt, writer->audio_encoder->channels, 1);
    if (fifo == NULL)
    {
        fprintf(stderr, "Could not allocate FIFO\n");
        return -1;
    }

    for (int i = 0; i < nb_frames; i++)
    {
        frame = frames;
        ret = init_converted_samples(&converted_input_samples, writer->audio_encoder, frame->nb_samples);
        if (ret < 0)
        {
            fprintf(stderr, "swr_convert_frame error: %s\n", av_err2str(ret));
            return -1;
        }

        /* Convert the input samples to the desired output sample format.
            * This requires a temporary storage provided by converted_input_samples. */
        if (convert_samples((const uint8_t**)frame->extended_data, converted_input_samples, frame->nb_samples, swr_ctx) < 0)
        {
            return -1;
        }

        /* Add the converted input samples to the FIFO buffer for later processing. */
        if (add_samples_to_fifo(fifo, converted_input_samples, frame->nb_samples) < 0)
        {
            return -1;
        }

        frames++;
    }

    /* If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder. */
    while (av_audio_fifo_size(fifo) >= writer->audio_encoder->frame_size)
    {
        tmp->pts = writer->latest_audio_pts;

        /* Read as many samples from the FIFO buffer as required to fill the frame.
         * The samples are stored in the frame temporarily. */
        if (av_audio_fifo_read(fifo, (void**)tmp->data, writer->audio_encoder->frame_size) < writer->audio_encoder->frame_size) 
        {
            fprintf(stderr, "Could not read data from FIFO\n");
            return -1;
        }

        write_frame(writer->output_context, writer->audio_encoder, writer->output_context->streams[stNum], tmp, pkt);
        writer->latest_audio_pts += tmp->nb_samples;
    }

    av_frame_free(&tmp);

    if (swr_ctx != NULL)
    {
        swr_free(&swr_ctx);
    }

    if (converted_input_samples)
    {
        av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }

    return 0;
}

int sw_write_frames(StreamWriter* writer, enum AVMediaType type, AVFrame* frames, int nb_frames)
{
    if (type == AVMEDIA_TYPE_AUDIO)
    {
        return sw_write_audio_frames(writer, frames, nb_frames);
    }
    else if (type == AVMEDIA_TYPE_VIDEO)
    {
        return sw_write_video_frames(writer, frames, nb_frames);
    }

    return 0;
}

int sw_flush_to_file(StreamWriter* writer, const char* output, const char* format)
{
    AVFormatContext* outputFormat;

    /* allocate the output media context */
    avformat_alloc_output_context2(&outputFormat, NULL, format, output);
    if (!outputFormat)
    {
        fprintf(stderr, "Output format was not initialized.\n");
        return -1;
    }

    AVCodecContext* audio = NULL;
    if (writer->audio_encoder != NULL) {
        audio = allocate_audio_codec_context(
            outputFormat,
            writer->audio_encoder->codec_id,
            writer->audio_encoder->bit_rate,
            writer->audio_encoder->sample_rate,
            writer->audio_encoder->channel_layout,
            writer->audio_encoder->sample_fmt);
    }

    AVCodecContext* video = NULL;
    if (writer->video_encoder != NULL) {
        video = allocate_video_codec_context(
            outputFormat,
            writer->video_encoder->codec_id,
            writer->video_encoder->time_base,
            writer->video_encoder->bit_rate,
            writer->video_encoder->width,
            writer->video_encoder->height,
            writer->video_encoder->pix_fmt);
    }

    FifoContext* fifo = writer->output_context->priv_data;

    return 0;
}