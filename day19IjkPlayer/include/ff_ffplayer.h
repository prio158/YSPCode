#ifndef FFPLAYER_H
#define FFPLAYER_H

#include <thread>
#include "ff_ffplayer.h"
#include "ffmsg_queue.h"
#include "ff_ffplay_def.h"

class FFPlayer
{

private:
    char *input_filename_;
    const char *FFPlayer_TAG = "FFPlayer";
    int abort_request = 0;
    int audio_stream = -1;
    int video_steam = -1;
    MessageQueue msg_queue_;

    std::thread *read_thread_;
    // 帧队列
    FrameQueue pictq;
    FrameQueue sampq;
    // Packet队列
    PacketQueue audioq;
    PacketQueue videoq;

public:
    FFPlayer();
    ~FFPlayer();
    int ffp_create();
    void ffp_destory();
    int ffp_prepare_async_l(char *file_name);
    int read_thread();
    // 播放控制
    int ffp_start_l();
    int ffp_stop_l();
    int stream_open(const char *file_name);
    void stream_close();
    /* 打开指定stream对应解码器，创建解码线程、以及初始化对应的输出 */
    int stream_component_open(int stream_index);
    /* 关闭指定stream的解码线程，释放解码器资源 */
    int stream_component_close(int stream_index);
};

/* 信号通知方法 */
inline static void ffp_notify_msg1(FFPlayer *ffp, int what)
{
}

inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1)
{
}

inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2)
{
}

inline static void ffp_notify_msg4(FFPlayer *ffp, int what, int arg1, int arg2, void *obj, int obj_len)
{
}

inline static void ffp_remove_msg(FFPlayer *ffp, int what)
{
}

#endif