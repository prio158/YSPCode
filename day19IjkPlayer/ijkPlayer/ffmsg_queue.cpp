#include "ffmsg_queue.h"
#include "log.h"

void msg_queue_init(MessageQueue *q)
{
    player_log_info(MESSAGE_QUEUE_TAG, "msg_queue_init");
}

void msg_queue_flush(MessageQueue *q)
{
}

void msg_queue_destroy(MessageQueue *q)
{
}

void msg_queue_abort(MessageQueue *q)
{
}

void msg_queue_start(MessageQueue *q)
{
}

int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    return 0;
}

void msg_queue_remove(MessageQueue *q, int what)
{
}

void msg_free_res(AVMessage *msg)
{
}

int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
    return 0;
}

int msg_queue_put(MessageQueue *q, AVMessage *msg)
{
    return 0;
}

void msg_init_msg(AVMessage *msg)
{
}

void msg_queue_put_simple1(MessageQueue *q, int what)
{
}

void msg_queue_put_simple2(MessageQueue *q, int what, int arg1)
{
}

void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2)
{
}

void msg_obj_free_l(void *obj)
{
}

void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len)
{
}
