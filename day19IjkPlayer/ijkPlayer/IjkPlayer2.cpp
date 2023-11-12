#include "../include/IjkPlayer2.h"
#include "IjkPlayer2.h"

IjkPlayer2::IjkPlayer2()
{
}

IjkPlayer2::~IjkPlayer2()
{
}

int IjkPlayer2::ijkmp_create(std::function<int(void *)> msg_loop)
{
    return 0;
}

int IjkPlayer2::ijkmp_destory()
{
    return 0;
}

int IjkPlayer2::ijkmp_set_data_source(const char *url)
{
    return 0;
}

int IjkPlayer2::ijkmp_prepare_async()
{
    return 0;
}

int IjkPlayer2::ijkmp_start()
{
    return 0;
}

int IjkPlayer2::ijkmp_stop()
{
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
    return 0;
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
