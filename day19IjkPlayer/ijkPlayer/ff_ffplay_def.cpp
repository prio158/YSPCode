#include "ff_ffplay_def.h"
#include "log.h"
#include <string>
#include <sstream>
#include "util.h"
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
        else if (q->nb_packets - 1 == index)
        {
            node += "pkt_" + std::to_string(index);
            q->print_to_string = node;
            player_log_info(PACKET_QUEUE_TAG, node.c_str());
        }
        index++;
    }

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
// TODO
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size)
{
    return 0;
}

void frame_queue_destory(FrameQueue *f)
{
}

void frame_queue_signal(FrameQueue *f)
{
}

Frame *frame_queue_peek(FrameQueue *f)
{
    return nullptr;
}

Frame *frame_queue_peek_next(FrameQueue *f)
{
    return nullptr;
}

Frame *frame_queue_peek_last(FrameQueue *f)
{
    return nullptr;
}

Frame *frame_queue_peek_writable(FrameQueue *f)
{
    return nullptr;
}

Frame *frame_queue_peek_readable(FrameQueue *f)
{
    return nullptr;
}

void frame_queue_push(FrameQueue *f)
{
}

void frame_queue_next(FrameQueue *f)
{
}

int frame_queue_nb_remaining(FrameQueue *f)
{
    return 0;
}

int64_t frame_queue_last_pos(FrameQueue *f)
{
    return 0;
}
