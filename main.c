#include <libavformat/avformat.h>

int main(int argc, const char *argv[]) {
    // TODO. Check that argc is > 2
    AVFormatContext *p_format_context = avformat_alloc_context();

    // Open the file and read the header, exporting the information stored there into the format context. Some formats
    // do not have a header or do not store enough information there, avformat_find_stream_info tries to read and decode
    // a few frames to find missing information
    avformat_open_input(&p_format_context, argv[1], NULL, NULL);
    printf("Format %s, duration %lld us\n", p_format_context->iformat->long_name, p_format_context->duration);
    avformat_find_stream_info(p_format_context, NULL);

    int video_stream_index = -1;
    const AVCodec *p_codec = NULL;
    const AVCodecParameters *p_codec_parameters = NULL;
    for (int i = 0; i < p_format_context->nb_streams; ++i) {
        const AVCodecParameters *p_local_codec_parameters = p_format_context->streams[i]->codecpar;
        const AVCodec *p_local_codec = avcodec_find_decoder(p_local_codec_parameters->codec_id);
        if (p_local_codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index == -1) {
            video_stream_index = i;
            p_codec = p_local_codec;
            p_codec_parameters = p_local_codec_parameters;
        }
        printf("Codec %s ID %d bit rate %lld\n", p_local_codec->name, p_local_codec->id,
               p_local_codec_parameters->bit_rate);
    }
    // TODO. Check that video_stream_index is not -1
    AVCodecContext *p_codec_context = avcodec_alloc_context3(p_codec);
    avcodec_parameters_to_context(p_codec_context, p_codec_parameters);

    // Cleanup
    avformat_close_input(&p_format_context);
}
