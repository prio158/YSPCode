#include "ff_ffplayer.h"
#include "log.h"
#include "ff_ffplay_def.h"
#include "ffmsg.h"
#include <iostream>

FFPlayer::FFPlayer()
{
    player_log_info(FFPlayer_TAG, "FFPlayer构造");
}

FFPlayer::~FFPlayer()
{
    player_log_info(FFPlayer_TAG, "FFPlayer析构");
}

int FFPlayer::ffp_create()
{
    player_log_info(FFPlayer_TAG, "ffp_create");
    msg_queue_init(&msg_queue_);
    return 0;
}

void FFPlayer::ffp_destory()
{
    player_log_info(FFPlayer_TAG, "ffp_destory");
    stream_close();
    msg_queue_destroy(&msg_queue_);
}

int FFPlayer::ffp_prepare_async_l(char *file_name)
{
    player_log_info(FFPlayer_TAG, "ffp_prepare_async_l");
    input_filename_ = file_name;
    int reval = stream_open(file_name);
    return reval;
}

// TODO
int FFPlayer::ffp_start_l()
{
    player_log_info(FFPlayer_TAG, "ffp_start_l");
    return 0;
}

int FFPlayer::ffp_stop_l()
{
    abort_request = 1;
    msg_queue_abort(&msg_queue_); // 禁止再插入消息
    return 0;
}

int FFPlayer::stream_open(const char *file_name)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "Did you set the DISPLAY variable?\n");
        return -1;
    }
    /* 初始化 Frame 帧队列 */
    if (frame_queue_init(&pictq, &videoq, VIDEO_PICTURE_QUEUE_SIZE_DEFAULT) < 0)
    {
        goto fail;
    }

    if (frame_queue_init(&sampq, &audioq, SAMPLE_QUEUE_SIZE) < 0)
    {
        goto fail;
    }

    // 初始化Packet包队列
    if (packet_queue_init(&videoq) < 0 ||
        packet_queue_init(&audioq) < 0)
        goto fail;

    // 初始化时钟

    // 初始化音量等

    // 创建解复用器读数据线程 read_thread
    read_thread_ = new std::thread(&FFPlayer::read_thread, this);

    return 0;

fail:
    stream_close();
    return -1;
}

void FFPlayer::stream_close()
{
    abort_request = 1; // 请求退出
    /* https://blog.csdn.net/KingOfMyHeart/article/details/96481709 */
    /* 一个可加入的线程是指一个线程已经启动，并且还没有被加入或分离。
     * 所以此时这个线程才能 join 或 detach，否则会一个已经加入的线程调用
     * join 或 detach会引起崩溃
     */
    if (read_thread_ && read_thread_->joinable())
    {
        read_thread_->join(); // 等待read_thread线程退出
    }

    /* 关闭解复用器 */
    // stream_component_close();

    /* 释放队列 */
    packet_queue_destroy(&videoq);
    packet_queue_destroy(&audioq);
    frame_queue_destory(&pictq);
    frame_queue_destory(&sampq);

    if (input_filename_)
    {
        free(input_filename_);
        input_filename_ = NULL;
    }
}

int FFPlayer::read_thread()
{
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
    player_log_info(FFPlayer_TAG, "read thread FFP_MSG_OPEN_INPUT");
    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO);
    player_log_info(FFPlayer_TAG, "read thread FFP_MSG_FIND_STREAM_INFO");
    ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);
    player_log_info(FFPlayer_TAG, "read thread FFP_MSG_COMPONENT_OPEN");
    ffp_notify_msg1(this, FFP_MSG_PREPARED);
    player_log_info(FFPlayer_TAG, "read thread FFP_MSG_PREPARED");

    while (1)
    {
        player_log_info(FFPlayer_TAG, "read thead is running...");
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
        if (abort_request)
        {
            break;
        }
    }
    std::cout << __FUNCTION__ << " leave" << std::endl;
    return 0;
}

/* 打开指定stream对应解码器，创建解码线程、以及初始化对应的输出 */
int FFPlayer::stream_component_open(int stream_index)
{
    AVCodecContext *avctx;
    AVCodec *codec;
    int sample_rate;
    int nb_channels;
    int64_t channel_layout;
    int ret = 0;

    /* stream_index是否合法 */
    if (stream_index < 0 || stream_index >= ic->nb_streams)
    {
        return -1;
    }
    /* 分配编码器上下文 */
    avctx = avcodec_alloc_context3(nullptr);
    if (!avctx)
        return AVERROR(ENOMEM);

    /* 将码流中的编解码器信息拷贝到新分配的编解码器上下文中 */
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;

    /* 设置 time base */
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder(avctx->codec_id);
    if (!codec)
    {
        av_log(NULL, AV_LOG_WARNING,
               "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = avcodec_open2(avctx, codec, nullptr)) < 0)
    {
        goto fail;
    }

    switch (avctx->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        /* 从AVCodecContext 中获取音频格式参数 */
        sample_rate = avctx->sample_rate;
        nb_channels = avctx->channels;
        channel_layout = avctx->channel_layout;
        /* 准备音频输出 */
        if ((ret = audio_open(channel_layout, nb_channels, sample_rate, &audio_tgt)) < 0)
        {
            goto fail;
        }
        audio_hw_buf_size = ret;
        audio_src = audio_tgt; // 暂且将数据源参数等同于目标输出参数
        audio_buf_size = 0;
        audio_buf_index = 0;
        audio_stream = stream_index;          // 获取 audio 的 stream 索引
        audio_st = ic->streams[stream_index]; // 获取 audio 的 stream 指针

        


        break;

    default:
        break;
    }

fail:
    avcodec_free_context(&avctx);

out:
    return ret;
}

int FFPlayer::stream_component_close(int stream_index)
{
    // TODO
    return 0;
}

/**
 * @param wanted_channel_layout
 * @param
 * @param
 * @param
 * @return 读取的字节数
 */
int FFPlayer::audio_open(int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, AudioParams *audio_hw_params)
{
    return 0;
}
