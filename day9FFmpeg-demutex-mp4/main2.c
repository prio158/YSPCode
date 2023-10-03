#include <stdio.h>
#include "libavformat/avformat.h"
#include <libavcodec/bsf.h>
#include "libavutil/log.h"

#define ERROR_STRING_SIZE 1024

const int sampling_frequencies[] = {
    96000, // 0x0
    88200, // 0x1
    64000, // 0x2
    48000, // 0x3
    44100, // 0x4
    32000, // 0x5
    24000, // 0x6
    22050, // 0x7
    16000, // 0x8
    12000, // 0x9
    11025, // 0xa
    8000   // 0xb
    // 0xc d e f是保留的
};

int adts_header(char *const p_adts_header, const int data_length,
                const int profile, const int samplerate,
                const int channels)
{
    int sampling_frequency_index = 3; // 默认初始化位3===>48kHZ采样率
    int adtsLen = data_length + 7;    // adts Header占7个字节
    int frequency_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for (i = 0; i < frequency_size; i++)
    {
        if (sampling_frequencies[i] == samplerate)
        {
            sampling_frequency_index = i;
            break;
        }
    }

    p_adts_header[0] = 0xff; // syncword
    p_adts_header[1] = 0xf0;
    p_adts_header[1] |= (0 << 3); // ID:MPEG4
    p_adts_header[1] |= (0 << 1); // Layer: 00
    p_adts_header[1] |= 1;        // protection absent:1

    p_adts_header[2] = (profile << 6);                          // profile
    p_adts_header[2] |= (sampling_frequency_index & 0x0f) << 2; // sampling_frequency_index
    p_adts_header[2] |= (0 << 1);                               // private_bit
    p_adts_header[2] |= (channels & 0x04) >> 2;
}

int main(int argc, char **argv)
{
    // 1、main参数校验
    if (argc != 4)
    {
        printf("Please 4 input params");
        return -1;
    }

    char *input_path = argv[1];
    char *h264_out_path = argv[2];
    char *aac_out_path = argv[3];
    char errors[ERROR_STRING_SIZE + 1]; // 主要是用来缓存解析FFmpeg api返回值的错误string
    int ret = 0;

    // 2、打开输出文件
    int h264_fd_output = fopen(h264_out_path, "wb");
    if (h264_fd_output < 0)
    {
        fprintf(stderr, "Can`t open output file:%s\n", h264_out_path);
        // goto _FAIL;
        return -1;
    }

    int aac_fd_output = fopen(aac_out_path, "wb");
    if (aac_fd_output < 0)
    {
        fprintf(stderr, "Can`t open output file:%s\n", aac_out_path);
        // goto _FAIL;
        return -1;
    }

    // 3、Allocate an AVFormatContext.
    AVFormatContext *avfctx = avformat_alloc_context();

    // 4、打开输入文件
    int input_fd = avformat_open_input(avfctx, input_path, NULL, NULL);
    if (input_fd < 0)
    {
        av_strerror(input_fd, errors, ERROR_STRING_SIZE);
        fprintf(stderr, "Can`t open input file %s\n", errors);
        avformat_close_input(&avfctx);
        return -1;
    }

    // 5、获取输入文件的vedio_index,audio_index
    int vedio_index = av_find_best_stream(avfctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vedio_index < 0)
    {
        av_strerror(vedio_index, errors, ERROR_STRING_SIZE);
        fprintf(stderr, "Can`t get vedio_index by av_find_best_stream:%s\n", errors);
        avformat_close_input(&avfctx);
        return -1;
    }

    int audio_index = av_find_best_stream(avfctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_index < 0)
    {
        av_strerror(vedio_index, errors, ERROR_STRING_SIZE);
        fprintf(stderr, "Can`t get audio_index by av_find_best_stream:%s\n", errors);
        avformat_close_input(&avfctx);
        return -1;
    }

    // 6、h264_mp4toannexb
    // （1）构造H264bit流转换器
    const AVBitStreamFilter *bsfilter = av_bsf_get_by_name("h264_mp4toannexb"); // 对应面向对象的方法
    if (!bsfilter)
    {
        avformat_close_input(&avfctx);
        printf("av_bsf_get_by_name h264_mp4toannexb failed\n");
        return -1;
    }

    //(2) 构造H264bit流转换器的context
    AVBSFContext *bsf_ctx = NULL;
    ret = av_bsf_alloc(bsfilter, bsf_ctx);
    if (ret < 0)
    {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("av_bsf_alloc failed:%s\n", errors);
        avformat_close_input(&avfctx);
        return -1;
    }

    //(3) 将视频流的编码器的参数复制到bsf_ctx的输入流参数
    ret = avcodec_parameters_copy(bsf_ctx->par_in, avfctx->streams[vedio_index]->codecpar);
    if (ret < 0)
    {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("av_bsf_alloc failed:%s\n", errors);
        avformat_close_input(&avfctx);
        return -1;
    }

    //(4) 初始化bsf_ctx
    ret = av_bsf_init(bsf_ctx);
    if (ret < 0)
    {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("av_bsf_init failed:%s\n", errors);
        avformat_close_input(&avfctx);
        return -1;
    }

    // 7、创建packet，并初始化
    AVPacket *pkt = av_packet_alloc();
    av_init_packet(pkt);

    // 8、开始处理
    while (1)
    {
        //(1) 将数据读到pkt中
        ret = av_read_frame(avfctx, pkt);
        if (ret < 0)
        {
            av_strerror(ret, errors, ERROR_STRING_SIZE);
            printf("av_read_frame failed:%s\n", errors);
            avformat_close_input(&avfctx);
            break;
        }

        //(2) 处理视频流
        if (pkt->stream_index == vedio_index)
        {
            // 将pkt给bsFilter，进行转换：mp4 to annexb
            ret = av_bsf_send_packet(bsf_ctx, pkt);
            if (ret < 0)
            {
                av_strerror(ret, errors, ERROR_STRING_SIZE);
                printf("av_bsf_send_packet failed:%s\n", errors);
                avformat_close_input(&avfctx);
                av_packet_unref(pkt);
                continue;
            }

            while (1)
            {
                ret = av_bsf_receive_packet(bsf_ctx, pkt);
                if (ret != 0)
                {
                    break;
                }
                size_t size = fwrite(pkt->data, 1, pkt->size, h264_fd_output);
                av_log(NULL, AV_LOG_DEBUG, "h264 warning, length of writed data isn't equal pkt->size(%d, %d)\n",
                       size,
                       pkt->size);
            }
            av_packet_unref(pkt);
        }
        else if (pkt->stream_index == audio_index)
        {
            // 处理音频
            char adts_header_buf[7] = {0};
            adts_header(adts_header_buf, pkt->size,
                        avfctx->streams[audio_index]->codecpar->profile,
                        avfctx->streams[audio_index]->codecpar->sample_rate,
                        avfctx->streams[audio_index]->codecpar->channels);
        }
        else
        {
            av_packet_unref(pkt);
        }
    }
}