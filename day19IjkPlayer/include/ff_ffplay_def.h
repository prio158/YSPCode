#ifndef FF_FFPLAT_DEF_H
#define FF_FFPLAT_DEF_H

#ifdef __cplusplus
extern "C"
{
#endif
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
#include <libavcodec/avcodec.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
}
#endif

#include <iostream>
#include <vector>

#define FRAME_QUEUE_SIZE 16
#define VIDEO_PICTURE_QUEUE_SIZE 3 // 图像帧缓存数量
#define VIDEO_PICTURE_QUEUE_SIZE_MIN (3)
#define VIDEO_PICTURE_QUEUE_SIZE_MAX (16)
#define VIDEO_PICTURE_QUEUE_SIZE_DEFAULT (VIDEO_PICTURE_QUEUE_SIZE_MIN)
#define SUBPICTURE_QUEUE_SIZE 16 // 字幕帧缓存数量
#define SAMPLE_QUEUE_SIZE 9      // 采样帧缓存数量
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

static const char *PACKET_QUEUE_TAG = "PACKET_QUEUE";
static const char *FRAME_QUEUE_TAG = "FRAME_QUEUE";

typedef struct MyAVPacketList
{
    AVPacket pkt;                // 解封装后的数据(未解码)
    struct MyAVPacketList *next; // 下一个节点
    int serial;                  // 播放序列（每次 seek 都会 +1）
};

typedef struct PacketQueue
{
    MyAVPacketList *first_pkt, *last_pkt; // 对头、队尾指针
    int nb_packets;                       // 包数量
    int size;                             // 队列所有元素的数据大小总和
    int64_t duration;                     // 队列所有元素的数据播放持续时间
    int abort_request;                    // 用户退出请求标志
    int serial;                           // 播放序列（每次 seek 都会 +1）
    SDL_mutex *mutex;                     // 互斥量 (在队列中基本都会互斥量和条件变量，用于队列入队出队时的原子性，以及维护生产-消费模型）
    SDL_cond *cond;                       // 条件变量
    std::string print_to_string = "";     // 类似 java 中的 toString 方法，用于调试
} PacketQueue;

/* 缓存解码后的数据结构 */
typedef struct Frame
{
    AVFrame *frame;  // 数据帧
    double pts;      // 这一帧的播放时刻
    double duration; // 持续时长
    int width;       // 图像宽度
    int height;      // 图像高度
    int format;      // 图像的Pixel Format
} Frame;

typedef struct FrameQueue
{
    Frame queue[FRAME_QUEUE_SIZE]; // FRAME_QUEUE_SIZE  最大size
    int rindex;                    // 读索引，可以理解为指向队头
    int windex;                    // 写索引
    int size;                      // 当前总帧数
    int max_size;                  // 可以存储的最大帧数
    SDL_mutex *mutex;              // 互斥量
    SDL_cond *cond;                // 条件变量
    PacketQueue *pktq;             // 数据包缓冲队列
} FrameQueue;

/* 音频参数 */
typedef struct AudioParams
{
    int freq;                // 采样率
    int channels;            // 通道数
    int64_t channel_layout;  // 通道布局
    enum AVSampleFormat fmt; // 音频采样格式
    int frame_size;          // 一个采样单元占用的字节数
    int bytes_per_sec;       // 一秒时间的字节数，比如采样率48KHZ，2 channels，16bit=48000*2*16/8 =192000byte
} AudioParams;

// 队列相关
int packet_queue_put_private(PacketQueue *q, AVPacket *pkt);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);
int packet_queue_init(PacketQueue *q);
void packet_queue_flush(PacketQueue *q);
void packet_queue_destroy(PacketQueue *q);
void packet_queue_abort(PacketQueue *q);
void packet_queue_start(PacketQueue *q);
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);
void init_packet_queue_info(PacketQueue *q);
void create_packet_queue_info(PacketQueue *q, std::vector<std::string> &input);

/* 初始化FrameQueue，视频和音频keep_last设置为1，字幕设置为0 */
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size);
void frame_queue_destory(FrameQueue *f);
void frame_queue_signal(FrameQueue *f);
/* 获取队列当前Frame, 在调用该函数前先调用frame_queue_nb_remaining确保有frame可读 */
Frame *frame_queue_peek(FrameQueue *f);

/* 获取当前Frame的下一Frame, 此时要确保queue里面至少有2个Frame */
// 不管你什么时候调用，返回来肯定不是 NULL
Frame *frame_queue_peek_next(FrameQueue *f);
/* 获取last Frame：
 */
Frame *frame_queue_peek_last(FrameQueue *f);
// 获取可写指针
Frame *frame_queue_peek_writable(FrameQueue *f);
// 获取可读
Frame *frame_queue_peek_readable(FrameQueue *f);
// 更新写指针
void frame_queue_push(FrameQueue *f);
/* 释放当前frame，并更新读索引rindex */
void frame_queue_next(FrameQueue *f);
int frame_queue_nb_remaining(FrameQueue *f);
int64_t frame_queue_last_pos(FrameQueue *f);

#endif