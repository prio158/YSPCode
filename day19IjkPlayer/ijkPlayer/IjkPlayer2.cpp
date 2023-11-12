#include "IjkPlayer2.h"

IjkPlayer2::IjkPlayer2()
{
    player_log_info(TAG.c_str(), "IjkPlayer2");
}

IjkPlayer2::~IjkPlayer2()
{
    player_log_info(TAG.c_str(), "~IjkPlayer2");
}

int IjkPlayer2::ijkmp_create(std::function<int(void *)> msg_loop)
{
    int ret = 0;
    ffplayer = new FFPlayer();
    if (!ffplayer)
    {
        player_log_error(TAG.c_str(), "new FFPlayer failed");
        return -1;
    }
    msg_loop_ = msg_loop;
    if (ffplayer->ffp_create() < 0)
    {
        player_log_error(TAG.c_str(), "ffplayer->ffp_create() failed");
        return -1;
    }

    return 0;
}

int IjkPlayer2::ijkmp_destory()
{
    ffplayer->ffp_destory();
    return 0;
}

int IjkPlayer2::ijkmp_set_data_source(const char *url)
{
    if (!url)
    {
        player_log_error(TAG.c_str(), "url is null in ijkmp_set_data_source");
        return -1;
    }

    return 0;
}

int IjkPlayer2::ijkmp_prepare_async()
{
    /* 状态：正在装备 */
    mp_state_ = MP_STATE_ASYNC_PREPARING;
    player_log_info(TAG.c_str(), "mp_state_:MP_STATE_ASYNC_PREPARING");
    /* 启动消息队列 */
    msg_queue_start(&ffplayer->msg_queue_);
    /* 创建 loop 线程 */
    msg_thread_ = new std::thread(&IjkPlayer2::ijkmp_msg_loop, this, this);
    /* 调用 ffplayer */
    if (ffplayer->ffp_prepare_async_l(data_source_) < 0)
    {
        mp_state_ = MP_STATE_ERROR;
        player_log_error(TAG.c_str(), "ffp_prepare_async_l fail in ijkmp_prepare_async");
        return -1;
    }
    return 0;
}

int IjkPlayer2::ijkmp_start()
{
    ffp_notify_msg1(ffplayer, FFP_REQ_START);
    return 0;
}

int IjkPlayer2::ijkmp_stop()
{
    int retval = ffplayer->ffp_stop_l();
    if (retval < 0)
    {
        player_log_error(TAG.c_str(), "ffp_stop_l fail in ijkmp_stop");
        return retval;
    }
    return 0;
}

int IjkPlayer2::ijkmp_pause()
{   

    return 0;
}

int IjkPlayer2::ijkmp_seek_to(long msec)
{
    return 0;
}

int IjkPlayer2::ijkmp_get_state()
{
    return mp_state_;
}

bool IjkPlayer2::ijkmp_is_playing()
{
    return false;
}

long IjkPlayer2::ijkmp_get_current_position()
{
    return 0;
}

long IjkPlayer2::ijkmp_get_duration()
{
    return 0;
}

long IjkPlayer2::ijkmp_get_playable_duration()
{
    return 0;
}

void IjkPlayer2::ijkmp_set_loop(int loop)
{

}

int IjkPlayer2::ijkmp_get_msg(AVMessage *msg, int block)
{
    return 0;
}

void IjkPlayer2::ijkmp_set_playback_volume(float volume)
{
}

int IjkPlayer2::ijkmp_msg_loop(void *arg)
{
    return 0;
}
