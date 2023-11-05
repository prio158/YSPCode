#include "ff_ffplayer.h"
#include "log.h"
#include "ff_ffplay_def.h"
#include "ffmsg.h"
#include <iostream>

FFPlayer::FFPlayer()
{
}

FFPlayer::~FFPlayer()
{
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

int FFPlayer::ffp_start_l()
{
    player_log_info(FFPlayer_TAG, "ffp_start_l");
    std::cout << __FUNCTION__;
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

int FFPlayer::stream_component_open(int stream_index)
{   
    //TODO
    return 0;
}

int FFPlayer::stream_component_close(int stream_index)
{
    //TODO
    return 0;
}
