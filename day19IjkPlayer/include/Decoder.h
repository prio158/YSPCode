#ifndef DECODER_H
#define DECODER_H

#include "ffmsg_queue.h"
#include "ff_ffplay_def.h"
#include "log.h"
#include <thread>

class Decoder
{
private:
    const char *TAG = "Decoder";

public:
    AVPacket pkt_;
    PacketQueue *queue_;    // 数据包队列
    AVCodecContext *avctx_; // 解码器上下文
    int pkt_serial_;        // 包序列
    int finished_;          // =0,解码器处于工作状态；!=0解码器处于空闲状态
    std::thread *decoder_thread_ = nullptr;
    Decoder();
    ~Decoder();
    void decoder_init(AVCodecContext *avctx, PacketQueue *queue);
    /* 创建启动线程 */
    int decoder_start(enum AVMediaType code_type, const char *thread_name, void *arg);
    /* 停止线程 */
    void decoder_abort(FrameQueue *fq);
    void decoder_destory();
    int decoder_decode_frame(AVFrame *frame);
    int queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
    int audio_thread(void *arg);
    int video_thread(void *arg);
    int get_video_frame(AVFrame *frame);
};

#endif