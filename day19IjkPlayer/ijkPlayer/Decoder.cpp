#include "ff_ffplayer.h"
#include "Decoder.h"

Decoder::Decoder()
{
    av_init_packet(&pkt_);
}

Decoder::~Decoder()
{
}

void Decoder::decoder_init(AVCodecContext *avctx, PacketQueue *queue)
{
    avctx_ = avctx;
    queue_ = queue;
}

int Decoder::decoder_start(AVMediaType code_type, const char *thread_name, void *arg)
{
    /* 启动包队列 */
    packet_queue_start(queue_);
    /* 创建线程 */
    if (code_type == AVMEDIA_TYPE_VIDEO)
    {
        decoder_thread_ = new std::thread(&Decoder::video_thread, this, arg);
    }
    else if (code_type == AVMEDIA_TYPE_AUDIO)
    {
        decoder_thread_ = new std::thread(&Decoder::audio_thread, this, arg);
    }
    else
        return -1;

    return 0;
}

void Decoder::decoder_abort(FrameQueue *fq)
{
    packet_queue_abort(queue_);
    frame_queue_signal(fq); // 唤醒阻塞的帧队列
    if (decoder_thread_ && decoder_thread_->joinable())
    {
        decoder_thread_->join(); // 等待解码线程退出
        delete decoder_thread_;
        decoder_thread_ = nullptr;
    }
    packet_queue_flush(queue_);
}

void Decoder::decoder_destory()
{
    av_packet_unref(&pkt_);
    avcodec_free_context(&avctx_);
}

/**
 * @return -1: 请求退出
 *          0: 解码已经结束，不再有数据可以读取
 *          1: 获取到解码后的 frame
 */
int Decoder::decoder_decode_frame(AVFrame *frame)
{
    int ret = AVERROR(EAGAIN);
    for (;;)
    {
        AVPacket pkt;
        do
        {
            if (queue_->abort_request)
            {
                player_log_info(TAG, "abort_request in decoder_decode_frame");
                return -1;
            }
            switch (avctx_->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
                ret = avcodec_receive_frame(avctx_, frame);
                if (ret < 0)
                {
                    char errStr[256] = {0};
                    av_strerror(ret, errStr, sizeof(errStr));
                    player_log_error(TAG, errStr);
                }
                break;

            case AVMEDIA_TYPE_AUDIO:
                ret = avcodec_receive_frame(avctx_, frame);
                if (ret >= 0)
                {
                    AVRational tb = (AVRational){1, frame->sample_rate};
                    if (frame->pts != AV_NOPTS_VALUE)
                    {
                        frame->pts = av_rescale_q(frame->pts, avctx_->pkt_timebase, tb);
                    }
                }
                else
                {
                    char errStr[256] = {0};
                    av_strerror(ret, errStr, sizeof(errStr));
                    player_log_error(TAG, errStr);
                }
                break;
            }
            /* 检查解码器是否已经结束 */
            if (ret == AVERROR_EOF)
            {
                player_log_info(TAG, "EOF in decoder_decode_frame");
                avcodec_flush_buffers(avctx_);
                return 0;
            }
            /* 正常解码返回 1 */
            if (ret >= 0)
                return 1;
        } while (ret != AVERROR(EAGAIN));

        /* 阻塞读取 packet */
        if (packet_queue_get(queue_, &pkt, 1, &pkt_serial_) < 0)
            return -1;

        if (avcodec_send_packet(avctx_, &pkt) == AVERROR(EAGAIN))
        {
            av_log(avctx_, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
        }
        av_packet_unref(&pkt);
    }

    return 0;
}

int Decoder::queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;
    // 此时vp指向fq的对头
    if (!(vp = frame_queue_peek_writable(fq)))
        return -1;
    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;
    vp->pts = pts;
    vp->duration = duration;

    av_frame_move_ref(vp->frame, src_frame);
    frame_queue_push(fq); // 更新写索引的位置
    return 0;
}

int Decoder::audio_thread(void *arg)
{
    player_log_info(TAG, "audio_thread is running");
    FFPlayer *is = (FFPlayer *)arg;
    AVFrame *frame = av_frame_alloc(); // 分配解码帧
    Frame *af;
    int got_frame = 0; // 是否读取到帧
    AVRational tb;     // timebase
    int ret = 0;
    if (!frame)
        return AVERROR(ENOMEM);

    do
    { /* 1、读取解码帧 */
        if ((got_frame = decoder_decode_frame(frame)) < 0)
            goto the_end;

        if (got_frame)
        {
            // 设置为sample_rate为timebase
            tb = (AVRational){1, frame->sample_rate};
            /* 获取可写的帧 */
            if (!(af = frame_queue_peek_writable(&is->sampq)))
                goto the_end;
            /* 转换时间戳 */
            af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});
            av_frame_move_ref(af->frame, frame);
            frame_queue_push(&is->sampq);
        }
        /*
            EAGAIN：需要新的输入数据才能返回新的输出。
            AVERROR_EOF：End of file
         */
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

the_end:
    std::cout << __FUNCTION__ << " leave " << std::endl;
    av_frame_free(&frame);
    return ret;
}

int Decoder::video_thread(void *arg)
{
    player_log_info(TAG, "video_thread is running...");
    FFPlayer *is = (FFPlayer *)arg;
    AVFrame *frame = av_frame_alloc(); // 分配解码帧
    double pts;                        // pts
    double duration;                   // 帧持续时间
    int ret;
    // 1 获取stream timebase
    AVRational tb = is->video_st->time_base; // 获取stream timebase
    // 2 获取帧率，以便计算每帧picture的duration
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

    if (!frame)
        return AVERROR(ENOMEM);

    for (;;)
    { // 循环取出视频解码的帧数据
        ret = get_video_frame(frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;
        // 1/帧率 = duration 单位秒, 没有帧率时则设置为0, 有帧率帧计算出帧间隔
        duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
        // 根据 AVStream timebase 计算出的 pts 值，单位为秒
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        // 将解码后的视频帧插入队列
        ret = queue_picture(&is->pictq, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial_);
        // 释放 frame 对应的数据
        av_frame_unref(frame);
        if (ret < 0)
            goto the_end;
    }

the_end:
    std::cout << __FUNCTION__ << " leave " << std::endl;
    av_frame_free(&frame);
    return 0;
}

int Decoder::get_video_frame(AVFrame *frame)
{
    int got_picture;
    // 1. 获取解码后的视频帧
    if ((got_picture = decoder_decode_frame(frame)) < 0)
    { // 返回-1 意味着要退出解码线程，所以要分析 decoder_decode_frame什么情况下返回-1
        return -1;
    }
    if (got_picture)
    {
    }
    return got_picture;
}
void Decoder::decoder_destroy()
{
    av_packet_unref(&pkt_);
    avcodec_free_context(&avctx_);
}
