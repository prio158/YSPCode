#ifndef IJKPLATER_H
#define IJKPLATER_H

#include <mutex>
#include <thread>
#include <functional>
#include "../include/ff_ffplayer.h"

class IjkPlayer2
{
private:
    /* 互斥量 */
    std::mutex mutex_;
    /* 真正的播放器 */
    FFPlayer *ffplayer = nullptr;
    /* 消息循环函数 */
    std::function<int(void *)> msg_loop_;
    /* 消息线程 */
    std::thread *msg_thread_;
    /* 播放的url */
    char *data_source_;
    /* 播放的状态：prepaered\resumed\error\completed */
    int mp_state_;

public:
    IjkPlayer2();
    ~IjkPlayer2();
    int ijkmp_create(std::function<int(void *)> msg_loop);
    int ijkmp_destory();
    /* 设置要播放的url */
    int ijkmp_set_data_source(const char *url);
    /* 准备播放 */
    int ijkmp_prepare_async();
    /* 触发播放 */
    int ijkmp_start();
    /* 停止 */
    int ijkmp_stop();
    /* 暂停 */
    int ijkmp_pause();
    /* seek */
    int ijkmp_seek_to(long msec);
    /* 获取播放状态 */
    int ijkmp_get_state();
    /* 判断是否在播放中 */
    bool ijkmp_is_playing();
    /* 获取当前的播放位置 */
    long ijkmp_get_current_position();
    /* 总长度 */
    long ijkmp_get_duration();
    /* 已经播放的长度 */
    long ijkmp_get_playable_duration();
    /* 设置循环播放 */
    void ijkmp_set_loop(int loop);
    /* 读取消息 */
    int ijkmp_get_msg(AVMessage *msg, int block);
    /* 设置音量 */
    void ijkmp_set_playback_volume(float volume);
    /* loop */
    int ijkmp_msg_loop(void *arg);
};

#endif
