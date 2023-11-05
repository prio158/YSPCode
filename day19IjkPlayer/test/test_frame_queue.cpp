#include "ff_ffplay_def.h"
#include "log.h"



int main(){

    FrameQueue* fp = new FrameQueue();
    PacketQueue* pq = new PacketQueue();
    frame_queue_init(fp,pq,32);

    frame_queue_destory(fp);

}

