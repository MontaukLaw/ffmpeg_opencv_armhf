#ifndef DERRYPLAYERKT_VIDEOCHANNEL_H
#define DERRYPLAYERKT_VIDEOCHANNEL_H

#include "BaseChannel.h"

extern "C" {
    #include <libswscale/swscale.h> // 视频画面像素格式转换的模块
	#include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
};

typedef void(*RenderCallback) (uint8_t *, int, int, int); // 函数指针声明定义  // TODO 第三节课新增

class VideoChannel : public BaseChannel {
private:
    pthread_t pid_video_decode;
    pthread_t pid_video_play;
    RenderCallback renderCallback; // TODO 第三节课新增

    int fps; // fps是视频通道独有的，fps（一秒钟多少帧）

public:
    VideoChannel(int stream_index, AVCodecContext *codecContext, int fps);
    ~VideoChannel();

    void start();
    void stop();

    void video_decode();
    void video_play();
    void setRenderCallback(RenderCallback renderCallback); // TODO 第三节课新增
};

#endif //DERRYPLAYERKT_VIDEOCHANNEL_H
