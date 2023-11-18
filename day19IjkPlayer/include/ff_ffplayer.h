#ifndef FFPLAYER_H
#define FFPLAYER_H

#include <thread>
#include "ff_ffplayer.h"
#include "ffmsg_queue.h"
#include "ff_ffplay_def.h"
#include "Decoder.h"

class FFPlayer
{

public:
    char *input_filename_;
    const char *FFPlayer_TAG = "FFPlayer";
    int abort_request = 0;
    int audio_stream = -1;
    int video_stream = -1;
    MessageQueue msg_queue_;

    std::thread *read_thread_;
    // 帧队列
    FrameQueue pictq;
    FrameQueue sampq;
    // Packet队列
    PacketQueue audioq;
    PacketQueue videoq;
    /* Format I/O context. */
    AVFormatContext *ic = nullptr;
    struct AudioParams audio_src; // 保存最新解码的音频参数
    struct AudioParams audio_tgt; // 保存SDL音频输出需要的参
    double audio_clock = 0;       // 当前音频帧的PTS+当前帧Duration

    int audio_hw_buf_size = 0; // SDL音频缓冲区的大小(字节为单位)
    // 指向待播放的一帧音频数据，指向的数据区将被拷入SDL音频缓冲区。若经过重采样则指向audio_buf1，
    // 否则指向frame中的音频
    uint8_t *audio_buf = nullptr;                                        // 指向需要重采样的数据
    uint8_t *audio_buf1 = nullptr;                                       // 指向重采样后的数据
    unsigned int audio_buf_size = 0;                                     // 待播放的一帧音频数据(audio_buf指向)的大小
    unsigned int audio_buf1_size = 0;                                    // 申请到的音频缓冲区audio_buf1的实际尺寸
    int audio_buf_index = 0;                                             // 更新拷贝位置 当前音频帧中已拷入SDL音频缓冲区
    AVStream *audio_st = nullptr;                                        // 音频流
    AVStream *video_st = nullptr;                                        // 视频流
    Decoder auddec;                                                      // 音频解码器
    Decoder viddec;                                                      // 视频解码器
    struct SwrContext *swr_ctx = nullptr;                                // 音频重采样context
    std::thread *video_refresh_thread_ = nullptr;                        // 视频画面输出相关
    std::function<int(const Frame *)> video_refresh_callback_ = nullptr; // 播放视频的回调
    Clock audclk;                                                        // 音频时钟
    int av_sync_type = AV_SYNC_AUDIO_MASTER;                             // 音视频同步方式, 默认audio master
    int eof = 0;                                                         // 文件结尾标志位

public:
    FFPlayer();
    ~FFPlayer();
    int ffp_create();
    void ffp_destory();
    int ffp_prepare_async_l(char *file_name);
    int read_thread();
    int video_refresh_thread();
    void video_refresh(double *remaining_time);
    int get_master_sync_type();
    double get_master_clock();
    void add_video_refresh_callback(std::function<int(const Frame *)> callback);

    // 播放控制
    int ffp_start_l();
    int ffp_stop_l();
    int stream_open(const char *file_name);
    void stream_close();
    /* 打开指定stream对应解码器，创建解码线程、以及初始化对应的输出 */
    int stream_component_open(int stream_index);
    /* 关闭指定stream的解码线程，释放解码器资源 */
    void stream_component_close(int stream_index);
    int audio_open(int64_t wanted_channel_layout,
                   int wanted_nb_channels, int wanted_sample_rate,
                   struct AudioParams *audio_hw_params);
    void audio_close();
};

/* 信号通知方法 */
inline static void ffp_notify_msg1(FFPlayer *ffp, int what)
{
    msg_queue_put_simple3(&ffp->msg_queue_, what, 0, 0);
}

inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1)
{
    msg_queue_put_simple3(&ffp->msg_queue_, what, arg1, 0);
}

inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2)
{
    msg_queue_put_simple3(&ffp->msg_queue_, what, arg1, arg2);
}

inline static void ffp_notify_msg4(FFPlayer *ffp, int what, int arg1, int arg2, void *obj, int obj_len)
{
    msg_queue_put_simple4(&ffp->msg_queue_, what, arg1, arg2, obj, obj_len);
}

inline static void ffp_remove_msg(FFPlayer *ffp, int what)
{
    msg_queue_remove(&ffp->msg_queue_, what);
}

#endif