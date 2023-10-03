#include "avpacket.h"
// #include "libavcodec/avcodec.h"

#define MEM_ITEM_SIZE (20*1024*102)
#define AVPACKET_LOOP_COUNT 1000
// 测试 内存泄漏
/**
 * @brief 测试av_packet_alloc和av_packet_free的配对使用
 */
void av_packet_test1()
{
    AVPacket *pkt = NULL;
    int ret = 0;

    pkt = av_packet_alloc(); //分配AVPacket内存，但里面的buf指针为null，没有初始化
    ret = av_new_packet(pkt, MEM_ITEM_SIZE); //为buf指针分配内存，并且引用计数初始化为1
    memccpy(pkt->data, (void *)&av_packet_test1, 1, MEM_ITEM_SIZE);
    av_packet_unref(pkt);       // 1、内部会释放掉pkt中的buf所指向的内存，2、buf指针重新赋值为null
    av_packet_free(&pkt);       // 如果不free将会发生内存泄漏,内部调用了 av_packet_unref
}

/**
 * @brief 测试误用av_init_packet将会导致内存泄漏
 */
void av_packet_test2()
{
    AVPacket *pkt = NULL;
    int ret = 0;

    pkt = av_packet_alloc();
    ret = av_new_packet(pkt, MEM_ITEM_SIZE);
    memccpy(pkt->data, (void *)&av_packet_test1, 1, MEM_ITEM_SIZE);
//    av_init_packet(pkt);        // 这个时候init就会导致内存无法释放
    av_packet_free(&pkt);
}

/**
 * @brief 测试av_packet_move_ref后，可以av_init_packet
 */
void av_packet_test3()
{
    AVPacket *pkt = NULL;
    AVPacket *pkt2 = NULL;
    int ret = 0;

    pkt = av_packet_alloc();
    ret = av_new_packet(pkt, MEM_ITEM_SIZE);
    memccpy(pkt->data, (void *)&av_packet_test1, 1, MEM_ITEM_SIZE);
    pkt2 = av_packet_alloc();   // 必须先alloc
    av_packet_move_ref(pkt2, pkt);//内部其实也调用了av_init_packet
    av_init_packet(pkt);
    av_packet_free(&pkt);
    av_packet_free(&pkt2);
}
/**
 * @brief 测试av_packet_clone
 */
void av_packet_test4()
{
    AVPacket *pkt = NULL;
    // av_packet_alloc()没有必要，因为av_packet_clone内部有调用 av_packet_alloc
    AVPacket *pkt2 = NULL;
    int ret = 0;

    pkt = av_packet_alloc();
    ret = av_new_packet(pkt, MEM_ITEM_SIZE);
    memccpy(pkt->data, (void *)&av_packet_test1, 1, MEM_ITEM_SIZE);
    pkt2 = av_packet_clone(pkt); // av_packet_alloc()+av_packet_ref()， 调用该函数后，pkt和pkt2对应的buf引用计数变成2
    av_init_packet(pkt);  // 这里是故意去做init的操作，让这个函数出现内存泄漏
    av_packet_free(&pkt);   // pkt在调用av_init_packet后，对应的buf被置为NULL，在调用av_packet_free没法做引用计数-1的操作
    av_packet_free(&pkt2); // 触发引用计数变为1，但因为不是0，所以buf不会被释放，导致内存泄漏
}

/**
 * @brief 测试av_packet_ref
 */
void av_packet_test5()
{
    AVPacket *pkt = NULL;
    AVPacket *pkt2 = NULL;
    int ret = 0;

    pkt = av_packet_alloc(); //
    if(pkt->buf)        // 打印referenc-counted，必须保证传入的是有效指针
    {    printf("%s(%d) ref_count(pkt) = %d\n", __FUNCTION__, __LINE__,
               av_buffer_get_ref_count(pkt->buf));
    }

    ret = av_new_packet(pkt, MEM_ITEM_SIZE);
    if(pkt->buf)        // 打印referenc-counted，必须保证传入的是有效指针
    {    printf("%s(%d) ref_count(pkt) = %d\n", __FUNCTION__, __LINE__,
               av_buffer_get_ref_count(pkt->buf));
    }
    memccpy(pkt->data, (void *)&av_packet_test1, 1, MEM_ITEM_SIZE);

    pkt2 = av_packet_alloc();   // 必须先alloc
    av_packet_move_ref(pkt2, pkt); // av_packet_move_ref
//    av_init_packet(pkt);  //av_packet_move_ref

    int count1 = av_buffer_get_ref_count(pkt2->buf); //count = 1
    //int count_pkt1 = av_buffer_get_ref_count(pkt->buf); //count = 0
    av_packet_ref(pkt, pkt2);     //将pkt（dst）中的buf 指向pkt2（src）中的buf
    int count2 = av_buffer_get_ref_count(pkt2->buf); // count = 2
     int count_pkt2 = av_buffer_get_ref_count(pkt->buf); //count = 2
    av_packet_ref(pkt, pkt2);     // 多次ref如果没有对应多次unref将会内存泄漏
    int count3 = av_buffer_get_ref_count(pkt2->buf); // count = 3
     int count_pkt3 = av_buffer_get_ref_count(pkt->buf); //count = 3
    if(pkt->buf)        // 打印referenc-counted，必须保证传入的是有效指针
    {    printf("%s(%d) ref_count(pkt) = %d\n", __FUNCTION__, __LINE__,
               av_buffer_get_ref_count(pkt->buf));
    }
    if(pkt2->buf)        // 打印referenc-counted，必须保证传入的是有效指针
    {    printf("%s(%d) ref_count(pkt) = %d\n", __FUNCTION__, __LINE__,
               av_buffer_get_ref_count(pkt2->buf));
    }
    av_packet_unref(pkt);   // 将为2
    av_packet_unref(pkt);   // 做第二次是没有用的
    if(pkt->buf)
        printf("pkt->buf没有被置NULL\n");
    else
        printf("pkt->buf已经被置NULL\n");
    if(pkt2->buf)        // 打印referenc-counted，必须保证传入的是有效指针
    {    printf("%s(%d) ref_count(pkt) = %d\n", __FUNCTION__, __LINE__,
               av_buffer_get_ref_count(pkt2->buf));
    }
    av_packet_unref(pkt2);


    av_packet_free(&pkt);
    av_packet_free(&pkt2);
}

/**
 * @brief 测试AVPacket整个结构体赋值, 和av_packet_move_ref类似
 */
void av_packet_test6()
{
    AVPacket *pkt = NULL;
    AVPacket *pkt2 = NULL;
    int ret = 0;

    pkt = av_packet_alloc();
    ret = av_new_packet(pkt, MEM_ITEM_SIZE);
    memccpy(pkt->data, (void *)&av_packet_test1, 1, MEM_ITEM_SIZE);

    pkt2 = av_packet_alloc();   // 必须先alloc
    *pkt2 = *pkt;   // 有点类似  pkt可以重新分配内存
    av_init_packet(pkt);

    av_packet_free(&pkt);
    av_packet_free(&pkt2);
}

void av_packet_test()
{
//    av_packet_test1();
//    av_packet_test2();
//    av_packet_test3();
//    av_packet_test4();
   av_packet_test5();
//    av_packet_test6();
}
