#include "ff_ffplayer.h"
#include "log.h"
#include "ff_ffplay_def.h"
#include "ffmsg.h"
#include <iostream>

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

FFPlayer::FFPlayer()
{
    player_log_info(FFPlayer_TAG, "FFPlayer构造");
}

FFPlayer::~FFPlayer()
{
    player_log_info(FFPlayer_TAG, "FFPlayer析构");
}

int FFPlayer::ffp_create()
{
    player_log_info(FFPlayer_TAG, "ffp_create");
    msg_queue_init(&msg_queue_);
    return 0;
}

void FFPlayer::ffp_destory()
{
    player_log_info(FFPlayer_TAG, "ffp_destory");
    stream_close();
    msg_queue_destroy(&msg_queue_);
}

int FFPlayer::ffp_prepare_async_l(char *file_name)
{
    player_log_info(FFPlayer_TAG, "ffp_prepare_async_l");
    input_filename_ = file_name;
    int reval = stream_open(file_name);
    return reval;
}

// TODO
int FFPlayer::ffp_start_l()
{
    player_log_info(FFPlayer_TAG, "ffp_start_l");
    return 0;
}

int FFPlayer::ffp_stop_l()
{
    abort_request = 1;
    msg_queue_abort(&msg_queue_); // 禁止再插入消息
    return 0;
}

int FFPlayer::stream_open(const char *file_name)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "Did you set the DISPLAY variable?\n");
        return -1;
    }
    /* 初始化 Frame 帧队列 */
    if (frame_queue_init(&pictq, &videoq, VIDEO_PICTURE_QUEUE_SIZE_DEFAULT) < 0)
    {
        goto fail;
    }

    if (frame_queue_init(&sampq, &audioq, SAMPLE_QUEUE_SIZE) < 0)
    {
        goto fail;
    }

    // 初始化Packet包队列
    if (packet_queue_init(&videoq) < 0 ||
        packet_queue_init(&audioq) < 0)
        goto fail;

    // 初始化时钟

    // 初始化音量等

    // 创建解复用器读数据线程 read_thread
    read_thread_ = new std::thread(&FFPlayer::read_thread, this);

    return 0;

fail:
    stream_close();
    return -1;
}

void FFPlayer::stream_close()
{
    abort_request = 1; // 请求退出
    /* https://blog.csdn.net/KingOfMyHeart/article/details/96481709 */
    /* 一个可加入的线程是指一个线程已经启动，并且还没有被加入或分离。
     * 所以此时这个线程才能 join 或 detach，否则会一个已经加入的线程调用
     * join 或 detach会引起崩溃
     */
    if (read_thread_ && read_thread_->joinable())
    {
        read_thread_->join(); // 等待read_thread线程退出
    }

    /* 关闭解复用器 */
    // stream_component_close();

    /* 释放队列 */
    packet_queue_destroy(&videoq);
    packet_queue_destroy(&audioq);
    frame_queue_destory(&pictq);
    frame_queue_destory(&sampq);

    if (input_filename_)
    {
        free(input_filename_);
        input_filename_ = NULL;
    }
}

// TODO
int FFPlayer::read_thread()
{

    return 0;
}

/* 打开指定stream对应解码器，创建解码线程、以及初始化对应的输出 */
int FFPlayer::stream_component_open(int stream_index)
{
    AVCodecContext *avctx;
    AVCodec *codec;
    int sample_rate;
    int nb_channels;
    int64_t channel_layout;
    int ret = 0;

    /* stream_index是否合法 */
    if (stream_index < 0 || stream_index >= ic->nb_streams)
    {
        return -1;
    }
    /* 分配编码器上下文 */
    avctx = avcodec_alloc_context3(nullptr);
    if (!avctx)
        return AVERROR(ENOMEM);

    /* 将码流中的编解码器信息拷贝到新分配的编解码器上下文中 */
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;

    /* 设置 time base */
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder(avctx->codec_id);
    if (!codec)
    {
        av_log(NULL, AV_LOG_WARNING,
               "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = avcodec_open2(avctx, codec, nullptr)) < 0)
    {
        goto fail;
    }

    switch (avctx->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        /* 从AVCodecContext 中获取音频格式参数 */
        sample_rate = avctx->sample_rate;
        nb_channels = avctx->channels;
        channel_layout = avctx->channel_layout;
        /* 准备音频输出 */
        if ((ret = audio_open(channel_layout, nb_channels, sample_rate, &audio_tgt)) < 0)
        {
            goto fail;
        }
        audio_hw_buf_size = ret;
        audio_src = audio_tgt; // 暂且将数据源参数等同于目标输出参数
        audio_buf_size = 0;
        audio_buf_index = 0;
        audio_stream = stream_index;          // 获取 audio 的 stream 索引
        audio_st = ic->streams[stream_index]; // 获取 audio 的 stream 指针

        /* 初始化 ffplay 封装的音频解码器，并将解码器上下文 avctx 和 Decoder 绑定 */
        auddec.decoder_init(avctx, &audioq);
        /* 启动音频解码线程 */
        auddec.decoder_start(AVMEDIA_TYPE_AUDIO, "audio_thread", this);
        // play audio 允许音频输出
        SDL_PauseAudio(0);
        break;

    case AVMEDIA_TYPE_VIDEO:

        break;
    }

fail:
    avcodec_free_context(&avctx);

out:
    return ret;
}

void FFPlayer::stream_component_close(int stream_index)
{
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
    {
        return;
    }
    codecpar = ic->streams[stream_index]->codecpar;
    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        player_log_info(FFPlayer_TAG, "stream_component_close audio");
        auddec.decoder_abort(&sampq);
        audio_close();
        auddec.decoder_destory();
        swr_free(&swr_ctx);
        av_freep(&audio_buf1);
        audio_buf1_size = 0;
        audio_buf = nullptr;
        break;

    case AVMEDIA_TYPE_VIDEO:

        if (video_refresh_thread_ && video_refresh_thread_->joinable())
        {
            video_refresh_thread_->join(); // 等待线程退出
        }
        player_log_info(FFPlayer_TAG, "stream_component_close vedio");

        // 请求终止解码器线程
        viddec.decoder_abort(&pictq);
        // 关闭音频设备
        // 销毁解码器
        viddec.decoder_destroy();
        break;
    }
    /* 复位 */
    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        audio_st = nullptr;
        audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        video_st = NULL;
        video_stream = -1;
        break;
    default:
        break;
    }
}

/**
 * wanted_spec是期望的参数，spec是实际的参数
 * @param  用户指定的channel_layout
 * @param  用户指定的nb_channels
 * @param  用户指定的wanted_sample_rate
 * @param  用户指定的hw_params
 * @return 读取的字节数
 */
int FFPlayer::audio_open(int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec;
    /* 音频参数设置 */
    wanted_spec.freq = wanted_sample_rate;     // 采样率
    wanted_spec.format = AUDIO_S16SYS;         // 采样格式
    wanted_spec.channels = wanted_nb_channels; // 2通道
    wanted_spec.silence = 0;
    wanted_spec.samples = 2048;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;

    /* 打开音频设备 */
    if (SDL_OpenAudio(&wanted_spec, nullptr) != 0)
    {
        player_log_error(FFPlayer_TAG, SDL_GetError());
        return -1;
    }
    // spec.spec是SDL 硬件支持的参数，audio_hw_params是外部传进来装的容器
    // audio_hw_params保存的参数，就是在做重采样的时候要转成的格式。
    /* 注意下面的参数应该是 SDL 支持的硬件参数，也就是SDL_OpenAudio的第二个参数，但是这里设置为null了
       下面直接用重采样后的参数赋值
     */
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = wanted_spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels = wanted_spec.channels;
    /* audio_hw_params->frame_size这里只是计算一个采样点占用的字节数 */
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels,
                                                             1,
                                                             audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels,
                                                                audio_hw_params->freq,
                                                                audio_hw_params->fmt, 1);

    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    // 比如2帧数据，一帧就是1024个采样点， 1024*2*2 * 2 = 8192字节
    return wanted_spec.size; /* SDL内部缓存的数据字节, samples * channels *byte_per_sample */
}

void FFPlayer::audio_close()
{
    SDL_CloseAudio(); 
}

/* prepare a new audio buffer */
/**
 * 对于何时调用callback回调函数，文档指出：Callback function for filling the audio buffer，
 * 应该是音频设备准备好之后开始定时通过callback取回数据到stream中。
 * @brief sdl_audio_callback
 * @param opaque    指向user的数据,类似于context的作用，主要是通过它可以访问到用户的数据
 * @param stream    指向需要填充的音频缓冲区
 * @param len       表示音频缓存区的大小
 */
static void sdl_audio_callback(void *opaque, Uint8 *steam, int len)
{
    FFPlayer *is = (FFPlayer *)opaque;
    int audio_size, len1;
    while (len > 0) // 循环读取，直到读取到足够的数据
    {
        /* 每次判断缓存区是否填满，如果没有填满，则检查解码得到的buf是否用完
           (1)如果is->audio_buf_index < is->audio_buf_size则说明
            is->audio_buf没有用完，则继续从buf中拷贝到stream，并记录已拷贝
            的字节数，移动stream指针以及索引，记录stream缓冲区剩余要拷贝的字节数 

           (2)如果is->audio_buf_index >= is->audio_buf_size,
           代表audio_buf消耗完了，则调用audio_decode_frame重新填充audio_buf
         */
        if (is->audio_buf_index >= is->audio_buf_size)
        {
            /* 
               audio_decode_frame中：
               1、取出FrameQueue中队头的 Frame
               2、将 Frame（重采样后) 拷贝到 is->audio_buf
               3、返回拷贝的 size （单位：字节）
            */
            audio_size = audio_decode_frame(is);
            if (audio_size < 0)
            {
                is->audio_buf = nullptr;
                is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE;
            }
            else
            {
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }

        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        /* 在audio_decode_frame中is->audio_buf会重新塞满新的 Frame 数据
           这个时候，将is->audio_buf中的数据拷贝到stream
         */
        if (is->audio_buf)
            memcpy(steam, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        steam += len1;
        /* 更新is->audio_buf_index，指向audio_buf中未被拷贝到stream的数据（剩余数据）的起始位置 */
        is->audio_buf_index += len1;    
    }

    if(!isnan(is->audio_clock)){
        //设置时钟
        set_clock(&is->audclk,is->audio_clock);
    }
}

static int audio_decode_frame(FFPlayer *is)
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    int wanted_nb_samples;
    Frame *af;
    int ret = 0;

    // 若队列头部可读，则由af指向可读帧
    if (!(af = frame_queue_peek_readable(&is->sampq)))
        return -1;

    /* 根据 frame 中指定的音频参数获取缓冲区的大小：af->frame->channels*af->frame->nb_samples*2 */
    data_size = av_samples_get_buffer_size(nullptr,
                                           af->frame->channels,
                                           af->frame->nb_samples,
                                           (enum AVSampleFormat)af->frame->format, 1);
    /* 获取声道布局 */
    dec_channel_layout = (af->frame->channel_layout &&
                                  af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)
                              ? af->frame->channel_layout
                              : av_get_default_channel_layout(af->frame->channels));

    // 获取样本数校正值：若同步时钟是音频，则不调整样本数；否则根据同步需要调整样本数
    // 目前不考虑音视频同步
    wanted_nb_samples = af->frame->nb_samples;

    // is->audio_tgt是SDL可接受的音频帧数，是audio_open()中取得的参数(它最后一个参数数)
    // 在audio_open()函数中又有"is->audio_src = is->audio_tgt,所以下面audio_src其实就是audio_tgt"
    // 此处表示：如果frame中的音频参数 == is->audio_src == is->audio_tgt，
    // 那音频重采样的过程就免了(因此时is->swr_ctr是NULL) 因为硬件支持的参数和原始的参数一致，就不需要重采样
    // 否则使用frame(源)和is->audio_tgt(目标)中的音频参数来设置is->swr_ctx，
    // 并使用frame中的音频参数来赋值is->audio_src
    if (af->frame->format != is->audio_src.fmt ||
        dec_channel_layout != is->audio_src.channel_layout ||
        af->frame->sample_rate != is->audio_src.freq)
    {
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(
            nullptr,
            is->audio_tgt.channel_layout,
            is->audio_tgt.fmt,
            is->audio_tgt.freq,
            dec_channel_layout,
            (enum AVSampleFormat)af->frame->format,
            af->frame->sample_rate, 0, nullptr);

        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0)
        {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                   af->frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)af->frame->format), af->frame->channels,
                   is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            ret = -1;
            goto fail;
        }
        /* 之前audio_src暂时使用audio_tgt 初始化，这肯定是不对的
         * audio_src本身代表源参数，所以下面就是源参数赋值过去
         */
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (enum AVSampleFormat)af->frame->format;
    }

    if (is->swr_ctx)
    {
        /* swr_convert 第二个参数：输出音频缓冲区 */
        uint8_t *out = is->audio_buf1;

        /* swr_convert 第三个参数：输出音频缓冲区 size */
        // 高采样率往低采样率转换时得到更少的样本数量，比如 96k->48k, wanted_nb_samples=1024
        // 则wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate 为1024*48000/96000 = 512
        // +256 的目的是重采样内部是有一定的缓存，就存在上一次的重采样还缓存数据和这一次重采样一起输出的情况，所以目的是多分配输出buffer
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;

        /* 计算对应样本占据的大小 size */
        int out_size = av_samples_get_buffer_size(nullptr, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);

        if (out_size < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            ret = -1;
            goto fail;
        }

        /* 重采样第4个参数*/
        /* AVFrame.data数组大小固定为8，如果声道数超过8，需要从frame.extended_data获取声道数据。 */
        const uint8_t **in = (const uint8_t **)af->frame->extended_data; // data[0] data[1]

        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);

        // 音频重采样：len2返回值是重采样后得到的音频数据中单个声道的样本数
        int len2 = swr_convert(is->swr_ctx, &out, out_count, in, af->frame->nb_samples);
        if (len2 < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            ret = -1;
            goto fail;
        }

        if (len2 == out_count)
        { // 这里的意思是我已经多分配了buffer，实际输出的样本数不应该超过我多分配的数量
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        // 重采样返回的一帧音频数据大小(以字节为单位)
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    }
    else
    {
        // 未经重采样，则将指针指向frame中的音频数据
        is->audio_buf = af->frame->data[0]; // s16交错模式data[0], fltp data[0] data[1]
        resampled_data_size = data_size;
    }

    if (!isnan(af->pts))
        is->audio_clock = af->pts;
    else
        is->audio_clock = NAN;

    frame_queue_next(&is->sampq);

    ret = resampled_data_size;

fail:
    return ret;
}