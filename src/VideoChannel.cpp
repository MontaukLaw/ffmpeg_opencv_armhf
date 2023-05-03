#include "VideoChannel.h"
#include <iostream>
using namespace std;

/**
 * 丢包 AVFrame * 原始包 很简单，因为不需要考虑 关键帧
 * @param q
 */
void dropAVFrame(queue<AVFrame *> &q)
{
    if (!q.empty())
    {
        AVFrame *frame = q.front();
        BaseChannel::releaseAVFrame(&frame);
        q.pop();
    }
}

/**
 * 丢包 AVPacket * 压缩包 考虑关键帧
 * @param q
 */
void dropAVPacket(queue<AVPacket *> &q)
{
    while (!q.empty())
    {
        AVPacket *pkt = q.front();
        if (pkt->flags != AV_PKT_FLAG_KEY)
        { // 非关键帧，可以丢弃
            BaseChannel::releaseAVPacket(&pkt);
            q.pop();
        }
        else
        {
            break; // 如果是关键帧，不能丢，那就结束
        }
    }
}

VideoChannel::VideoChannel(int stream_index, AVCodecContext *codecContext, AVRational time_base, int fps)
    : BaseChannel(stream_index, codecContext, time_base),
      fps(fps)
{
    frames.setSyncCallback(dropAVFrame);
    packets.setSyncCallback(dropAVPacket);
}

VideoChannel::~VideoChannel()
{
    // TODO 播放器收尾 3
    // DELETE(audio_channel);
}

void VideoChannel::stop()
{
    // TODO 播放器收尾 3

    pthread_join(pid_video_decode, nullptr);
    pthread_join(pid_video_play, nullptr);

    isPlaying = false;
    packets.setWork(0);
    frames.setWork(0);

    packets.clear();
    frames.clear();
}

// TODO =============================  下面是 解码出 原始包

void *task_video_decode(void *args)
{
    auto *video_channel = static_cast<VideoChannel *>(args);
    video_channel->video_decode();
    return nullptr;
}

// TODO 第五节课 内存泄漏关键点（控制frames队列大小，等待队列中的数据被消费） 2
// 1.把队列里面的压缩包（AVPacket *）取出来，然后解码成（AVFrame *）原始包 ----> 保存队列【真正干活的就是他】
void VideoChannel::video_decode()
{
    AVPacket *pkt = nullptr;
    while (isPlaying)
    {
        // 2.1 内存泄漏点
        if (isPlaying && frames.size() > 100)
        {
            av_usleep(10 * 1000); // 单位：microseconds 微妙 10毫秒
            continue;
        }

        int ret = packets.getQueueAndDel(pkt); // 阻塞式函数 取出刚刚DerryPlayer中加入的pkt
        if (!isPlaying)
        {
            break; // 如果关闭了播放，跳出循环，releaseAVPacket(&pkt);
        }

        if (!ret)
        {             // ret == 0
            continue; // 哪怕是没有成功，也要继续（假设：你生产太慢(压缩包加入队列)，我消费就等一下你）
        }

        ret = avcodec_send_packet(codecContext, pkt); // 第一步：把我们的 压缩包 AVPack发送给 FFmpeg缓存区

        // FFmpeg源码内部 缓存了一份pkt副本，所以我才敢大胆的释放
        // releaseAVPacket(&pkt); // 不是说不释放，而是放到后面去

        if (ret)
        {          // r != 0
            break; // avcodec_send_packet 出现了错误，结束循环
        }

        // 第二步：读取 FFmpeg缓存区 A里面的 原始包 ，有可能读不到，为什么？ 内部缓冲区 会 运作过程比较慢
        AVFrame *frame = av_frame_alloc();
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN))
        {
            // B帧  B帧参考前面成功  B帧参考后面失败   可能是P帧没有出来，再拿一次就行了
            continue;
        }
        else if (ret != 0)
        {
            // TODO 第五节课 解码视频的frame出错，马上释放，防止你在堆区开辟了控件 2.1 内存泄漏点
            if (frame)
            {
                releaseAVFrame(&frame);
            }
            break; // 出错误了
        }

        if (frames.size() == 0)
        {
            // 终于拿到 原始包了，加入队列-- YUV数据
            frames.insertToQueue(frame);
        }
        else
        {
            av_frame_unref(frame);
            releaseAVFrame(&frame);
        }
        // cout << packets.size() << endl;
        // TODO 第五节课 内存泄漏点 4
        // 安心释放pkt本身空间释放 和 pkt成员指向的空间释放
        av_packet_unref(pkt);  // 减1 = 0 释放成员指向的堆区
        releaseAVPacket(&pkt); // 释放AVPacket * 本身的堆区空间

    } // end while
    // TODO 第五节课 内存泄漏点 4.1
    av_packet_unref(pkt);  // 减1 = 0 释放成员指向的堆区
    releaseAVPacket(&pkt); // 释放AVPacket * 本身的堆区空间
}

// TODO =============================  下面是 播放 视频

void *task_video_play(void *args)
{
    auto *video_channel = static_cast<VideoChannel *>(args);
    video_channel->video_play();
    return nullptr;
}

// 2.把队列里面的原始包(AVFrame *)取出来， 播放 【真正干活的就是他】
void VideoChannel::video_play()
{ // 第二线线程：视频：从队列取出原始包，播放 【真正干活了】

    // SWS_FAST_BILINEAR == 很快 可能会模糊
    // SWS_BILINEAR == 适中的算法

    AVFrame *frame = nullptr;
    uint8_t *dst_data[4]; // RGBA
    int dst_linesize[4];  // RGBA

    // 原始包（YUV数据） -----> [libswscale]  Android屏幕（RGBA数据）

    // 给 dst_data 申请内存   width * height * 4 xxxx
    // av_image_alloc(dst_data, dst_linesize, codecContext->width, codecContext->height, AV_PIX_FMT_RGBA, 1);
    av_image_alloc(dst_data, dst_linesize, codecContext->width, codecContext->height, AV_PIX_FMT_BGR24, 1);
    // yuv -> rgba
    SwsContext *sws_ctx = sws_getContext(
        // 下面是输入环节
        codecContext->width,
        codecContext->height,
        codecContext->pix_fmt, // 自动获取 xxx.mp4 的像素格式  AV_PIX_FMT_YUV420P // 写死的

        // 下面是输出环节
        codecContext->width,
        codecContext->height,
        AV_PIX_FMT_BGR24,
        // AV_PIX_FMT_RGBA,
        SWS_BILINEAR, NULL, NULL, NULL);

    while (isPlaying)
    {
        int ret = frames.getQueueAndDel(frame);
        if (!isPlaying)
        {
            break; // 如果关闭了播放，跳出循环，releaseAVPacket(&pkt);
        }
        if (!ret)
        {             // ret == 0
            continue; // 哪怕是没有成功，也要继续（假设：你生产太慢(原始包加入队列)，我消费就等一下你）
        }

        // 格式转换 yuv ---> rgba
        sws_scale(sws_ctx,
                  // 下面是输入环节 YUV的数据
                  frame->data, frame->linesize,
                  0, codecContext->height,

                  // 下面是 输出环节 成果：RGBA数据 Android SurfaceView播放画面
                  dst_data,
                  dst_linesize);

        // 基础：数组被传递会退化成指针，默认就是去1元素
        renderCallback(dst_data[0], codecContext->width, codecContext->height, dst_linesize[0]);
        // releaseAVFrame(&frame); // 释放原始包，因为已经被渲染完了，没用了

        // TODO 第五节课 内存泄漏点 6
        av_frame_unref(frame);  // 减1 = 0 释放成员指向的堆区
        releaseAVFrame(&frame); // 释放AVFrame * 本身的堆区空间
    }
    // 简单的释放
    // releaseAVFrame(&frame); // 出现错误，所退出的循环，都要释放frame
    // TODO 第五节课 内存泄漏点 6.1
    av_frame_unref(frame);  // 减1 = 0 释放成员指向的堆区
    releaseAVFrame(&frame); // 释放AVFrame * 本身的堆区空间

    isPlaying = false;
    av_free(&dst_data[0]);
    sws_freeContext(sws_ctx); // free(sws_ctx); FFmpeg必须使用人家的函数释放，直接崩溃
}

// 视频：1.解码    2.播放
// 1.把队列里面的压缩包(AVPacket *)取出来，然后解码成（AVFrame * ）原始包 ----> 保存队列
// 2.把队列里面的原始包(AVFrame *)取出来， 播放
void VideoChannel::start()
{
    isPlaying = true;

    // 队列开始工作了
    packets.setWork(1); // 视频专属的packets队列
    frames.setWork(1);  // 音频专属的frames队列

    // 第一个线程： 视频：取出队列的压缩包 进行解码 解码后的原始包 再push队列中去（视频：YUV）
    pthread_create(&pid_video_decode, nullptr, task_video_decode, this);

    // 第二线线程：视频：从队列取出原始包，播放
    pthread_create(&pid_video_play, nullptr, task_video_play, this);
}

void VideoChannel::setRenderCallback(RenderCallback renderCallback)
{
    this->renderCallback = renderCallback;
}
