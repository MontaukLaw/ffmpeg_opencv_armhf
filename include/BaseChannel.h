#ifndef DERRYPLAYER_BASECHANNEL_H
#define DERRYPLAYER_BASECHANNEL_H

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/time.h>
};

#include "safe_queue.h"

class BaseChannel {

public:
    int stream_index; // 音频 或 视频 的下标
    SafeQueue<AVPacket *> packets; // 压缩的 数据包   AudioChannel.cpp(packets 1)   VideoChannel.cpp(packets 2)
    SafeQueue<AVFrame *> frames; // 原始的 数据包     AudioChannel.cpp(frames 3)   VideoChannel.cpp(frames 4)
    bool isPlaying; // 音频 和 视频 都会有的标记 是否播放
    AVCodecContext *codecContext = 0; // 音频 视频 都需要的 解码器上下文

    AVRational time_base; // TODO 音视频同步 2.1 （AudioChannel VideoChannel 都需要时间基）单位而已

    BaseChannel(int stream_index, AVCodecContext *codecContext, AVRational time_base)
            :
            stream_index(stream_index),
            codecContext(codecContext) ,
            time_base(time_base)  // 注意：这个接收的是 子类传递过来的 时间基
            {
        packets.setReleaseCallback(releaseAVPacket); // 给队列设置Callback，Callback释放队列里面的数据
        frames.setReleaseCallback(releaseAVFrame); // 给队列设置Callback，Callback释放队列里面的数据
    }

    virtual ~BaseChannel() {
        packets.clear();
        frames.clear();
    }

    /**
     * 释放 队列中 所有的 AVPacket *
     * @param packet
     */
    // typedef void (*ReleaseCallback)(T *);
    static void releaseAVPacket(AVPacket **p) {
        if (p) {
            av_packet_free(p); // 释放队列里面的 T == AVPacket
            *p = 0;
        }
    }

    /**
     * 释放 队列中 所有的 AVFrame *
     * @param packet
     */
    // typedef void (*ReleaseCallback)(T *);
    static void releaseAVFrame(AVFrame **f) {
        if (f) {
            av_frame_free(f); // 释放队列里面的 T == AVFrame
            *f = 0;
        }
    }
};

#endif //DERRYPLAYER_BASECHANNEL_H
