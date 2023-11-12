#include "ff_ffplay_def.h"
#include "log.h"
#include <string>
#include <sstream>
#include "util_player.h"
#include <vector>

static AVPacket flush_pkt;

/* PacketQueue 的操作 */
int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{

    MyAVPacketList *pkt1;

    if (q->abort_request)
    {
        player_log_error(PACKET_QUEUE_TAG, "packet_queue_put fail,because abort_request == 1");
        return -1;
    }

    pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));
    if (!pkt1)
    {
        player_log_error(PACKET_QUEUE_TAG, "MyAVPacketList malloc fail");
        return -1;
    }
    pkt1->pkt = *pkt; // 浅拷贝AVPacket，里面的 AVPacket.data 没有拷贝
    pkt1->next = nullptr;
    /* 如果放入 flush pkt，需要增加 serial 号，以区分不连续的两段数据 */
    if (pkt == &flush_pkt)
    {
        q->serial++;
        std::string print_str = "flush_pkt serial:" + std::to_string(q->serial);
        player_log_info(PACKET_QUEUE_TAG, print_str.c_str());
    }
    pkt1->serial = q->serial; // 用队列序列号标记节点

    if (!q->last_pkt)
    {
        q->first_pkt = pkt1;
    }
    else
    {
        q->last_pkt->next = pkt1;
    }
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;

    /* 发送信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了 */
    SDL_CondSignal(q->cond);
    return 0;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;
    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    SDL_UnlockMutex(q->mutex);
    if (pkt != &flush_pkt && ret < 0)
    {
        av_packet_unref(pkt);
    }

    return ret;
}

int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket *pkt1, *pkt = pkt1;
    av_init_packet(pkt);
    pkt->data = nullptr;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

int packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex)
    {
        player_log_error(PACKET_QUEUE_TAG, "SDL_CreateMutex fail in init stage");
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond)
    {
        player_log_error(PACKET_QUEUE_TAG, "SDL_CreateCondfail in init stage");
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1)
    {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = nullptr;
    q->first_pkt = nullptr;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    init_packet_queue_info(q);
    SDL_UnlockMutex(q->mutex);
}

void packet_queue_destroy(PacketQueue *q)
{
    // 先清除所有的结点
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond); // 终止的时候发送一个信号
    SDL_UnlockMutex(q->mutex);
}

void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
    SDL_UnlockMutex(q->mutex);
}

/**
 * @brief get packet from queue
 * @param packet queue
 * @param pkt输出参数，既MyAVPacketList.pkt
 * @param block 调用者是否需要在没结点可取的情况下阻塞等待
 * @param serial 输出参数
 * @return < 0 if aborted, 0 if no packet and > 0 if packet.
 */
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{

    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);
    for (;;)
    {
        if (q->abort_request)
        {
            ret = -1;
            break;
        }
        pkt1 = q->first_pkt; // 从队头拿数据
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
            {
                q->last_pkt = nullptr;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            q->duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            /* 如果调用方需要输出serial，那就输出给他 */
            if (serial)
            {
                *serial = pkt1->serial;
            }
            av_free(pkt1);
            ret = 1;

            if (!q->print_to_string.empty())
            {
                std::vector<std::string> output;
                splitWithString(q->print_to_string, "->", output);
                output.erase(output.begin());
                create_packet_queue_info(q, output);
            }

            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            /* 队列中没有数据，阻塞调用 */
            /* 这里没有break，而是 for 循环的另一个作用是在条件
               变量满足后重复上面代码取出结点。
             */
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex(q->mutex);
    return ret;
}

/* 遍历packet_queue中所有元素，组织信息打印 */
void init_packet_queue_info(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    MyAVPacketList *pkt, *pkt1;
    int index = 0;
    std::string node = "";
    for (pkt = q->first_pkt; pkt != nullptr; pkt = pkt->next)
    {
        if (pkt && q->nb_packets - 1 != index)
        {
            node += "pkt_" + std::to_string(index) + "->";
        }
        else if (pkt && q->nb_packets - 1 == index)
        {
            node += "pkt_" + std::to_string(index);
        }

        index++;
    }
    q->print_to_string = node;
    SDL_UnlockMutex(q->mutex);
}

void create_packet_queue_info(PacketQueue *q, std::vector<std::string> &input)
{
    std::string output = "";
    for (int i = 0; i < input.size(); i++)
    {
        output += input[i];
        if (i != input.size() - 1)
            output += "->";
    }
    q->print_to_string = output;
}

/* FrameQueue 的操作 */
// 初始化 FrameQueue，视频和音频 keep last 设置为 1，字幕设置为 0
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex()))
    {
        player_log_error(FRAME_QUEUE_TAG, "SDL_CreateMutex fail in frame_queue_init");
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = SDL_CreateCond()))
    {
        player_log_error(FRAME_QUEUE_TAG, "SDL_CreateCond fail in frame_queue_init");
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    for (i = 0; i < f->max_size; i++)
    {
        if (!(f->queue[i].frame = av_frame_alloc()))
        {
            player_log_error(FRAME_QUEUE_TAG, "av_frame_alloc fail in frame_queue_init");
            return AVERROR(ENOMEM);
        }
    }
    return 0;
}

void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++)
    {
        Frame *vp = &f->queue[i];
        av_frame_unref(vp->frame); // 释放 AVFrame 的数据缓存区，而不是释放 AVFrame
        av_frame_free(&vp->frame); // 释放 AVFrame 本身
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

void frame_queue_signal(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}
/* 获取队列当前Frame, 在调用该函数前先调用frame_queue_nb_remaining确保有frame可读  */
Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[f->rindex % f->max_size];
}

/* 获取当前Frame的下一Frame, 此时要确保queue里面至少有2个Frame */
Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + 1) % f->max_size];
}

Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

/* 获取可写指针,入队入口 */
Frame *frame_queue_peek_writable(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    /** 阻塞等待,直到有put frame 的空间
     * 生产者线程生产资源（产生一个对象），生产时间不确定
     * 缓冲区中里面没有剩余的空间时等待
     */
    /** 关于为什么使用while？多个消费者，一个生产者
     * ⚠️：一个生产者 一个消费者，这里可以使用 if。
     * eg：消费者 1、2 同时阻塞在SDL_CondWait(f->cond, f->mutex)上。
     * 此时，消费者1、2，被(生产者)条件变量同时被唤醒cond_signal，假设消费者1线程拿到锁，
     * 消费者线程2堵塞在这把锁上，然后消费者1从公共区域中拿数据后，释放锁，消费者线程2就拿到锁了，
     * 不堵塞，但是消费者 1 此时消费过数据了，此时的共享数据区域发生了改变。
     * 所以消费者2再被唤醒时，准备从公共区域中拿数据，但可能没数据了（被消费者1给消费了）。
     * 所以消费者1 释放锁时，消费者 2 虽然之前被 cond唤醒了，但是共享数据区域发生了改变，
     * 所以此时，消费者 2 需要重新判断条件变量是否满足，如果满足，还是走到 while 循环里面
     * SDL_CondWait，继续等待。否则直接向下走。如果用 if 就不会在重新判断一次：
     * f->size >= f->max_size && !f->pktq->abort_request，如果是 while，
     * SDL_CondWait在解除阻塞后，还是会执行一次判断的。
     *
     */
    while (f->size >= f->max_size &&
           !f->pktq->abort_request)
    {
        /** 条件变量：某个条件满足时（就是 while 里面的逻辑），线程继续执行，否则等待（阻塞）
         * 条件变量本身不是锁，但是它可以造成线程堵塞，通常与互斥锁配合，给多线程提供
         * 一个会合的场所。
         *
         * （1）阻塞等待条件变量满足：如果条件变量cond 不满足，就一直阻塞等待
         * （2）解锁已经成功加锁的信号量（也就是解锁f->mutex），这个锁主要是维持 1 的原子性
         * （3）当条件满足，函数返回，解除阻塞，并重新申请加互斥锁（f->mutex）,加锁条件变量。
         */
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request) /* 检查是否要退出 */
    {
        return nullptr;
    }
    return &f->queue[f->windex];
}

/* 获取可读指针 */
Frame *frame_queue_peek_readable(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);

    while (f->size <= 0 && !f->pktq->abort_request)
    {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (!f->pktq->abort_request)
        return nullptr;

    return &(f->queue[f->rindex % f->max_size]);
}

/* 更新写指针 */
void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond); // 当_readable在等待时则可以唤醒
    SDL_UnlockMutex(f->mutex);
}

/* 释放当前Frame，并更新读所有rindex */
void frame_queue_next(FrameQueue *f)
{
    av_frame_unref(f->queue[f->rindex].frame);
    if (++f->rindex == f->max_size)
    {
        f->rindex = 0;
    }
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

int frame_queue_nb_remaining(FrameQueue *f)
{
    return f->size;
}

/* return last shown position */
int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    return fp == nullptr ? -1 : f->rindex;
}
