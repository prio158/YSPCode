#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define BUF_SIZE 20480

int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    FILE *in_file = (FILE *)opaque;

    int read_size = fread(in_file, 1, BUF_SIZE, buf);
    if (read_size <= 0)
    {
        return AVERROR_EOF; // End of file
    }
    return read_size;
}

void decode(AVPacket *packet, AVFrame *frame, AVCodecContext *codec_context, FILE *outpunt_file)
{
}

int main(int argc, char **argv)
{

    // 1、参数校验
    if (argc != 3)
    {
        printf("please input file and output file");
        return -1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    // 2、打开文件
    FILE *input_file = fopen(input_path, 'rb');
    if (!input_file)
    {
        goto FAIL;
        return -1;
    }

    FILE *output_file = fopen(output_path, 'wb');
    if (!output_file)
    {
        goto FAIL;
        return -1;
    }

    // 3、自定义IO
    uint8_t *buffer = av_malloc(BUF_SIZE);
    AVIOContext *avio_context = avio_alloc_context(buffer, BUF_SIZE, 0, (void *)input_file, read_packet, NULL, NULL);

    // 4、打开输入文件，用AVFormatContext接收
    AVFormatContext *av_format_context = avformat_alloc_context();
    av_format_context->pb = avio_context;
    int ret = avformat_open_input(&av_format_context, input_file, NULL, NULL);
    if (ret < 0)
    {
        goto FAIL;
        return -1;
    }

    // 5、编码器
    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!codec)
    {
        goto FAIL;
        return -1;
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    if (!codec_context)
    {
        goto FAIL;
        return -1;
    }

    int ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0)
    {
        goto FAIL;
        return -1;
    }

    // 6、读取数据
    AVPacket *av_packet = av_packet_alloc();
    AVFrame *av_frame = av_frame_alloc();

    while (1)
    {
        int ret = av_read_frame(av_format_context, av_packet);
        if (ret < 0)
        {
            break;
        }
        decode(av_packet, av_frame, codec_context, output_file);
    }

    printf("read file finish\n");
    decode(NULL, av_frame, codec_context, output_file);

FAIL:
    if (input_file)
        fclose(input_file);
    if (output_file)
        fclose(output_file);
    if (buffer)
        av_free(buffer);

    if (av_frame)
        av_frame_free(av_frame);
    if (av_packet)
        av_packet_free(av_packet);

    // context释放
    if (av_format_context)
        avformat_close_input(&av_format_context);
    if (av_format_context)
        avformat_free_context(av_format_context);
    if (avio_context)
        avio_context_free(avio_context);
    if (codec_context)
        avcodec_free_context(&codec_context);
}