#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

// Decode packets into frames
static int decode_packet(const AVPacket *p_packet, AVCodecContext *p_codec_context, AVFrame *p_frame);

// Save a frame into a .ppm file
static void save_frame(const unsigned char *buf, int wrap, int x_size, int y_size, const char *filename);

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        printf("You need to specify a media file\n");
        return -1;
    }

    AVFormatContext *p_format_context = avformat_alloc_context();
    if (!p_format_context) {
        fprintf(stderr, "[ERROR] Could not allocate memory for format context\n");
        return -1;
    }

    // Open the file and read the header, exporting the information stored there into the format context.
    printf("Opening the input file (%s) and loading format header\n", argv[1]);
    if (avformat_open_input(&p_format_context, argv[1], NULL, NULL) != 0) {
        fprintf(stderr, "[ERROR] Could not open the input file (%s)\n", argv[1]);
        return -1;
    }
    printf("Format: %s\nDuration: %lld us\nBit rate: %lld\n", p_format_context->iformat->name,
           p_format_context->duration, p_format_context->bit_rate);

    // Some formats do not have a header or do not store enough information there, avformat_find_stream_info tries to
    // read and decode a few frames to find missing information
    if (avformat_find_stream_info(p_format_context, NULL) < 0) {
        fprintf(stderr, "[ERROR] Could not get stream info from the input file (%s)\n", argv[1]);
        return -1;
    }

    // Obtain the video codec checking all the streams in the format
    printf("Checking all the codecs in the format to find the proper codec:\n");
    int video_stream_index = -1;
    const AVCodec *p_codec = NULL; // Knows how to enCOde and DECode the stream
    const AVCodecParameters *p_codec_parameters = NULL; // Describe the properties of the codec used by the stream i
    for (int i = 0; i < p_format_context->nb_streams; ++i) {
        const AVCodecParameters *p_local_codec_parameters = p_format_context->streams[i]->codecpar;
        const AVCodec *p_local_codec = avcodec_find_decoder(p_local_codec_parameters->codec_id);
        if (p_local_codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index == -1) {
            video_stream_index = i;
            p_codec = p_local_codec;
            p_codec_parameters = p_local_codec_parameters;
        }
        printf("- Codec %s ID %d bit rate %lld\n", p_local_codec->name, p_local_codec->id,
               p_local_codec_parameters->bit_rate);
    }
    if (video_stream_index == -1) {
        fprintf(stderr, "[ERROR] Input file (%s) does not contain a video stream\n", argv[1]);
        return -1;
    }

    AVCodecContext *p_codec_context = avcodec_alloc_context3(p_codec);
    if (!p_codec_context) {
        fprintf(stderr, "[ERROR] Could not allocate memory for codec context\n");
        return -1;
    }

    // Fill the codec context based on the values from the supplied codec parameters
    if (avcodec_parameters_to_context(p_codec_context, p_codec_parameters) < 0) {
        fprintf(stderr, "[ERROR] Could not copy the codec parameters to the codec context\n");
        return -1;
    }

    // Initialize the codec context with the given codec
    if (avcodec_open2(p_codec_context, p_codec, NULL) < 0) {
        fprintf(stderr, "[ERROR] Could not open the selected codec\n");
        return -1;
    }

    AVPacket *p_packet = av_packet_alloc();
    if (!p_packet) {
        fprintf(stderr, "[ERROR] Could not allocate memory for packet\n");
        return -1;
    }
    AVFrame *p_frame = av_frame_alloc();
    if (!p_frame) {
        fprintf(stderr, "[ERROR] Could not allocate memory for frame\n");
        return -1;
    }

    // Read the packets from the stream and decode them into frames
    while (av_read_frame(p_format_context, p_packet) >= 0) {
        if (p_packet->stream_index == video_stream_index) {
            if (decode_packet(p_packet, p_codec_context, p_frame) < 0) break;
        }
        av_packet_unref(p_packet);
    }

    printf("Cleaning up all the resources\n");
    av_frame_free(&p_frame);
    av_packet_free(&p_packet);
    avformat_close_input(&p_format_context);

    return 0;
}

int decode_packet(const AVPacket *p_packet, AVCodecContext *p_codec_context, AVFrame *p_frame) {
    // Send the packet as input to the decoder
    int response = avcodec_send_packet(p_codec_context, p_packet);
    if (response < 0) {
        fprintf(stderr, "[ERROR] Could not send a packet to the codec: %s\n", av_err2str(response));
        return response;
    }

    // Create scaling context to convert from source format to RGB24
    struct SwsContext *sws_context = sws_getContext(
        p_codec_context->width, p_codec_context->height, p_codec_context->pix_fmt,
        p_codec_context->width, p_codec_context->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR,NULL,NULL,NULL
    );
    if (!sws_context) {
        fprintf(stderr, "[ERROR] Could not create scaling context\n");
        return -1;
    }

    AVFrame *p_frame_rgb = av_frame_alloc();
    if (!p_frame_rgb) {
        fprintf(stderr, "[ERROR] Could not allocate memory for frame\n");
        sws_freeContext(sws_context);
        return -1;
    }

    p_frame_rgb->format = AV_PIX_FMT_RGB24;
    p_frame_rgb->width = p_codec_context->width;
    p_frame_rgb->height = p_codec_context->height;

    int scaling_result = av_frame_get_buffer(p_frame_rgb, 0);
    if (scaling_result < 0) {
        fprintf(stderr, "[ERROR] Could not to allocate buffer for RGB frame: %s\n", av_err2str(scaling_result));
        sws_freeContext(sws_context);
        av_frame_free(&p_frame_rgb);
        return scaling_result;
    }

    while (1) {
        // Send the decoded output data from the codec to a frame
        response = avcodec_receive_frame(p_codec_context, p_frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        }
        if (response < 0) {
            fprintf(stderr, "[ERROR] Could not receive a frame from the decoder: %s\n", av_err2str(response));
            sws_freeContext(sws_context);
            av_frame_free(&p_frame_rgb);
            return response;
        }
        // Convert pixel format and scale to RGB24
        scaling_result = sws_scale(
            sws_context, // Scaling context
            p_frame->data, // Source data
            p_frame->linesize, // Source line sizes
            0, // Start line
            p_frame->height, // Number of lines to process
            p_frame_rgb->data, // Target data
            p_frame_rgb->linesize // Target line sizes
        );
        if (scaling_result != p_frame->height) {
            fprintf(stderr, "[ERROR] Could not perform scaling");
            sws_freeContext(sws_context);
            av_frame_free(&p_frame_rgb);
            return -1;
        }
        printf(
            "Frame %d (type=%c, size=%d bytes, format=%d) pts %p key_frame %d [DTS %d]\n",
            p_codec_context->frame_number,
            av_get_picture_type_char(p_frame_rgb->pict_type),
            p_frame_rgb->pkt_size,
            p_frame_rgb->format,
            &p_frame_rgb->pts,
            p_frame_rgb->key_frame,
            p_frame_rgb->coded_picture_number
        );
        char frame_filename[1024];
        snprintf(frame_filename, sizeof(frame_filename), "%s-%d.ppm", "frame",
                 p_codec_context->frame_number);
        save_frame(p_frame_rgb->data[0], p_frame_rgb->linesize[0], p_frame_rgb->width,
                   p_frame_rgb->height, frame_filename);
    }

    // Cleanup
    sws_freeContext(sws_context);
    av_frame_free(&p_frame_rgb);

    return 0;
}

void save_frame(const unsigned char *buf, const int wrap, const int x_size, const int y_size,
                const char *filename) {
    FILE *f = fopen(filename, "w");
    // Writing the minimal required header for a ppm file format (https://en.wikipedia.org/wiki/Netpbm_format#PPM_example)
    fprintf(f, "P6\n%d %d\n%d\n", x_size, y_size, 255);
    // Writing line by line
    for (int i = 0; i < y_size; i++)
        fwrite(buf + i * wrap, 1, x_size * 3, f);
    fclose(f);
}
