#include "ffmsg_queue.h"
#include "log.h"
extern "C"
{
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}

#include "ffmsg.h"

/* 消息队列初始化 */
void msg_queue_init(MessageQueue *q)
{
    player_log_info(MESSAGE_QUEUE_TAG, "msg_queue_init");
    memset(q, 0, sizeof(MessageQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    q->abort_request = 1;
}

/* 清空队列所有消息 */
void msg_queue_flush(MessageQueue *q)
{
    AVMessage *msg, *msg1;
    SDL_LockMutex(q->mutex);
    /* 把队列里面的消息全部丢入回收池中 */
    for (msg = q->first_msg; msg != nullptr; msg = msg1)
    {
        msg1 = msg->next;
        msg->next = q->recycle_msg; // 头插法
        q->recycle_msg = msg;
    }
    q->last_msg = nullptr;
    q->first_msg = nullptr;
    q->nb_messages = 0;
    SDL_UnlockMutex(q->mutex);
}

/* 销毁消息队列 */
void msg_queue_destroy(MessageQueue *q)
{
    msg_queue_flush(q);
    SDL_LockMutex(q->mutex);
    while (q->recycle_msg)
    {
        AVMessage *msg = q->recycle_msg;
        if (msg)
            q->recycle_msg = msg->next;
        msg_free_res(msg);
        av_freep(&msg);
    }
    SDL_UnlockMutex(q->mutex);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

/* 启动消息队列 */
void msg_queue_start(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    AVMessage *msg;
    msg_init_msg(msg);
    msg->what = FFP_MSG_FLUSH;
    msg_queue_put_private(q, msg);
    SDL_UnlockMutex(q->mutex);
}

void msg_queue_abort(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no msg and >0 if msg */
int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    AVMessage *msg1;
    int ret;
    SDL_LockMutex(q->mutex);
    for (;;)
    {
        if (q->abort_request)
        {
            ret = -1;
            break;
        }
        msg1 = q->first_msg;
        if (msg1)
        {
            q->first_msg = msg1->next;
            if (!q->first_msg)
            {
                q->last_msg = nullptr;
            }
            q->nb_messages--;
            *msg = *msg1;
            msg1->obj = nullptr;
            msg1->next = q->recycle_msg;
            q->recycle_msg = msg1;
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
        SDL_UnlockMutex(q->mutex);
        return ret;
    }

    return 0;
}

void msg_queue_remove(MessageQueue *q, int what)
{
    AVMessage **p_msg, *msg, *last_msg;
    SDL_LockMutex(q->mutex);

    last_msg = q->first_msg;

    if (!q->abort_request && q->first_msg)
    {
        p_msg = &q->first_msg;
        while (*p_msg)
        {
            msg = *p_msg;
            if (msg->what == what)
            { // 同类型的消息全部删除
                *p_msg = msg->next;
                msg_free_res(msg);
                msg->next = q->recycle_msg; // 消息体回收
                q->recycle_msg = msg;
                q->nb_messages--;
            }
            else
            {
                last_msg = msg;
                p_msg = &msg->next;
            }
        }

        if (q->first_msg)
        {
            q->last_msg = last_msg;
        }
        else
        {
            q->last_msg = NULL;
        }
    }

    SDL_UnlockMutex(q->mutex);
}

void msg_free_res(AVMessage *msg)
{
    if (!msg || !msg->obj)
        return;
    msg->free_l(msg->obj);
    msg->obj = NULL;
}

// 消息队列内部重新去构建 AVMessage（重新申请AVMessage，或者来自于recycle_msg）
// 新的消息插入到尾部
int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
    AVMessage *msg1;
    if (q->abort_request == 1)
    {
        return -1;
    }
    /* 从回收池中找一个现成的 */
    msg1 = q->recycle_msg;
    if (msg1) /* 如果找到了 */
    {
        q->recycle_msg = msg1->next;
        q->recycle_count++;
    }
    else /* 回收池没有 */
    {
        q->alloc_count++;
        msg1 = (AVMessage *)av_malloc(sizeof(AVMessage));
    }
    *msg1 = *msg;
    msg1->next = nullptr; /* 从回收池断开 */
    if (!q->first_msg)
    {
        q->first_msg = msg1;
    }
    else
    {
        q->last_msg->next = msg1;
    }
    q->last_msg = msg1;
    q->nb_messages++;
    SDL_CondSignal(q->cond);
    return 0;
}

int msg_queue_put(MessageQueue *q, AVMessage *msg)
{
    int ret;
    SDL_LockMutex(q->mutex);
    ret = msg_queue_put_private(q, msg);
    SDL_UnlockMutex(q->mutex);
    return ret;
}

void msg_init_msg(AVMessage *msg)
{
    memset(msg, 0, sizeof(AVMessage));
}

void msg_queue_put_simple1(MessageQueue *q, int what)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg_queue_put(q, &msg);
}

void msg_queue_put_simple2(MessageQueue *q, int what, int arg1)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg_queue_put(q, &msg);
}

void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg_queue_put(q, &msg);
}

void msg_obj_free_l(void *obj)
{
    av_free(obj);
}

void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg.obj = av_malloc(obj_len);
    memcpy(msg.obj, obj, obj_len);
    msg.free_l = msg_obj_free_l;
    msg_queue_put(q, &msg);
}
