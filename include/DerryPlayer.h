#ifndef DERRYPLAYER_DERRYPLAYER_H
#define DERRYPLAYER_DERRYPLAYER_H

#include <cstring>
#include <pthread.h>
#include "VideoChannel.h" // 可以直接访问函数指针
#include "util.h"

// extern “C“的作用是什么？ https://blog.csdn.net/qq_41709234/article/details/123082378
extern "C" { // ffmpeg是纯c写的，必须采用c的编译方式，否则奔溃
    #include <libavformat/avformat.h>
    #include <libavutil/time.h>
};

class DerryPlayer {
private:
    char *data_source = 0; // 指针 请赋初始值
    pthread_t pid_prepare;
    pthread_t pid_start;
    AVFormatContext *formatContext = 0; // 媒体上下文 封装格式
    VideoChannel *video_channel = 0;
    bool isPlaying; // 是否播放
    RenderCallback renderCallback;
    int duration; // TODO 第七节课增加 总时长

    pthread_mutex_t seek_mutex; // TODO 第七节课增加 3.1
    pthread_t pid_stop;

    bool ifReady = false;

public:
    DerryPlayer(const char *data_source);
    ~DerryPlayer();

    void prepare();
    void prepare_();
    void prepare__();


    void start();
    void start_();

    void setRenderCallback(RenderCallback renderCallback);

    int getDuration();

    void seek(int play_value);

    void stop();

    void stop_(DerryPlayer *);

    bool if_ready_to_start();
};

#endif //DERRYPLAYER_DERRYPLAYER_H
