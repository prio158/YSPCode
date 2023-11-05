#ifndef IJKPLATER_H
#define IJKPLATER_H

#include <mutex>
#include <thread>
#include <functional>
#include "../include/ff_ffplayer.h"


class IjkPlayer2
{
private:
    std::mutex mutex_;
    FFPlayer* ffplayer;
    std::function<int(void*)> msg_loop_;
    std::thread* msg_thread_;
    char* data_source_; //url
    int mp_state_;


public:
    IjkPlayer2(/* args */);
    ~IjkPlayer2();
    



};

IjkPlayer2::IjkPlayer2(/* args */)
{
}

IjkPlayer2::~IjkPlayer2()
{
}





#endif 
