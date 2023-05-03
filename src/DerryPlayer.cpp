#include "DerryPlayer.h"

DerryPlayer::DerryPlayer(const char *data_source)
{
    // this->data_source = data_source;
    // 如果被释放，会造成悬空指针

    // 深拷贝
    // this->data_source = new char[strlen(data_source)];
    // Java: demo.mp4
    // C层：demo.mp4\0  C层会自动 + \0,  strlen不计算\0的长度，所以我们需要手动加 \0

    this->data_source = new char[strlen(data_source) + 1];
    strcpy(this->data_source, data_source); // 把源 Copy给成员

    pthread_mutex_init(&seek_mutex, nullptr); // TODO 第七节课增加 3.1
}

DerryPlayer::~DerryPlayer()
{
    if (data_source)
    {
        delete data_source;
        data_source = nullptr;
    }

    pthread_mutex_destroy(&seek_mutex); // TODO 第七节课增加 3.1
}

// TODO >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  下面全部都是 prepare

// void* (*__start_routine)(void*)  子线程了
void *task_prepare(void *args)
{ // 此函数和DerryPlayer这个对象没有关系，你没法拿DerryPlayer的私有成员

    // avformat_open_input(0, this->data_source)

    auto *player = static_cast<DerryPlayer *>(args);
    player->prepare_();
    return nullptr; // 必须返回，坑，错误很难找
}

void DerryPlayer::prepare_()
{ // 属于 子线程了 并且 拥有  DerryPlayer的实例 的 this

    // 为什么FFmpeg源码，大量使用上下文Context？
    // 答：因为FFmpeg源码是纯C的，他不像C++、Java ， 上下文的出现是为了贯彻环境，就相当于Java的this能够操作成员

    /**
     * TODO 第一步：打开媒体地址（文件路径， 直播地址rtmp）
     */
    formatContext = avformat_alloc_context();

    // 字典（键值对）
    AVDictionary *dictionary = nullptr;
    // 设置超时（5秒）
    av_dict_set(&dictionary, "listen_timeout", "5000000", 0); // 单位微妙

    /**
     * 1，AVFormatContext *
     * 2，路径 url:文件路径或直播地址
     * 3，AVInputFormat *fmt  Mac、Windows 摄像头、麦克风， 我们目前安卓用不到
     * 4，各种设置：例如：Http 连接超时， 打开rtmp的超时  AVDictionary **options
     */
    int r = avformat_open_input(&formatContext, data_source, nullptr, &dictionary);
    // 释放字典
    av_dict_free(&dictionary);
    if (r)
    {
        avformat_close_input(&formatContext);
        return;
    }

    // 你在 xxx.mp4 能够拿到
    // 你在 xxx.flv 拿不到，是因为封装格式的原因
    // formatContext->duration;

    /**
     * TODO 第二步：查找媒体中的音视频流的信息
     */
    r = avformat_find_stream_info(formatContext, nullptr);
    if (r < 0)
    {
        avformat_close_input(&formatContext);
        return;
    }

    // 你在 xxx.mp4 能够拿到
    // 你在 xxx.flv 都能拿到
    // avformat_find_stream_info FFmpeg内部源码已经做（流探索）了，所以可以拿到 总时长
    this->duration = formatContext->duration / AV_TIME_BASE; // FFmpeg的单位 基本上都是  有理数(时间基)，所以你需要这样转

    AVCodecContext *codecContext = nullptr;

    /**
     * TODO 第三步：根据流信息，流的个数，用循环来找
     */
    for (int stream_index = 0; stream_index < formatContext->nb_streams; ++stream_index)
    {
        /**
         * TODO 第四步：获取媒体流（视频，音频）
         */
        AVStream *stream = formatContext->streams[stream_index];

        /**
         * TODO 第五步：从上面的流中 获取 编码解码的【参数】
         * 由于：后面的编码器 解码器 都需要参数（宽高 等等）
         */
        AVCodecParameters *parameters = stream->codecpar;

        /**
         * TODO 第六步：（根据上面的【参数】）获取编解码器
         */
        AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec)
        {
            // TODO 播放器收尾 1
            avformat_close_input(&formatContext);
        }

        /**
         * TODO 第七步：编解码器 上下文 （这个才是真正干活的）
         */
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext)
        {

            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
            avformat_close_input(&formatContext);

            return;
        }

        /**
         * TODO 第八步：他目前是一张白纸（parameters copy codecContext）
         */
        r = avcodec_parameters_to_context(codecContext, parameters);
        if (r < 0)
        {
            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
            avformat_close_input(&formatContext);
            return;
        }

        /**
         * TODO 第九步：打开解码器
         */
        r = avcodec_open2(codecContext, codec, nullptr);
        if (r)
        {
            // 非0就是true
            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
            avformat_close_input(&formatContext);
            return;
        }

        // TODO 音视频同步 2
        AVRational time_base = stream->time_base;

        if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO)
        {

            // 虽然是视频类型，但是只有一帧封面
            if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
            {
                continue;
            }

            // TODO 音视频同步 2.2 （视频独有的 fps值）
            AVRational fps_rational = stream->avg_frame_rate;
            int fps = av_q2d(fps_rational);

            // 是视频
            video_channel = new VideoChannel(stream_index, codecContext, time_base, fps);
            video_channel->setRenderCallback(renderCallback);
        }
    } // for end

    ifReady = true;
}

void DerryPlayer::prepare()
{
    // 问题：当前的prepare函数，子线程，还是，主线程？
    // 此函数是被MainActivity的onResume调用下来的（安卓的主线程）

    // 解封装 FFMpeg来解析   data_source 可以直接解析吗？
    // 答：data_source == 文件IO流  ，直播网络rtmp，  所以按道理来说，会耗时，所以必须使用子线程

    // 创建子线程 pthread
    pthread_create(&pid_prepare, nullptr, task_prepare, this); // this == DerryPlayer的实例
}

// TODO >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  下面全部都是 start
void *task_start(void *args)
{
    auto *player = static_cast<DerryPlayer *>(args);
    player->start_();
    return nullptr; // 必须返回，坑，错误很难找
}

// TODO 第五节课 内存泄漏关键点（控制packet队列大小，等待队列中的数据被消费） 1
// 把 视频 音频 的 压缩包（AVPacket *） 循环获取出来 加入到队列里面去
void DerryPlayer::start_()
{ // 子线程
    // 一股脑 把 AVPacket * 丢到 队列里面去  不区分 音频 视频
    while (isPlaying)
    {
        // 解决方案：视频 我不丢弃数据，等待队列中数据 被消费 内存泄漏点1.1
        if (video_channel && video_channel->packets.size() > 100)
        {
            av_usleep(10 * 1000); // 单位：microseconds 微妙 10毫秒
            continue;
        }

        // AVPacket 可能是音频 也可能是视频（压缩包）
        AVPacket *packet = av_packet_alloc();

        int ret = av_read_frame(formatContext, packet);
        if (!ret)
        {
            if (video_channel && video_channel->stream_index == packet->stream_index)
            {
                // 代表是视频
                video_channel->packets.insertToQueue(packet);
            }
        }
        else if (ret == AVERROR_EOF)
        { 
            // end of file == 读到文件末尾了 == AVERROR_EOF
            // TODO 1.3 内存泄漏点
            // TODO 表示读完了，要考虑释放播放完成，表示读完了 并不代表播放完毕，以后处理【同学思考 怎么处理】
            if (video_channel->packets.empty())
            {
                break; // 队列的数据被音频 视频 全部播放完毕了，我再退出
            }
        }
        else
        {
            break; // av_read_frame(formatContext,  packet); 出现了错误，结束当前循环
        }
    } // end while
    isPlaying = false;
    video_channel->stop();
}

void DerryPlayer::start()
{
    isPlaying = 1;

    // 视频：1.解码    2.播放
    // 1.把队列里面的压缩包(AVPacket *)取出来，然后解码成（AVFrame * ）原始包 ----> 保存队列
    // 2.把队列里面的原始包(AVFrame *)取出来， 视频播放
    if (video_channel)
    {
        video_channel->start(); // 视频的播放
    }
    // 把 音频 视频 压缩包  加入队列里面去
    // 创建子线程 pthread
    pthread_create(&pid_start, nullptr, task_start, this); // this == DerryPlayer的实例
}

void DerryPlayer::setRenderCallback(RenderCallback renderCallback)
{
    this->renderCallback = renderCallback;
}

// TODO 第七节课增加 获取总时长
int DerryPlayer::getDuration()
{
    return duration; // 在调用此函数之前，必须给此duration变量赋值
}

void DerryPlayer::seek(int progress)
{
    // 健壮性判断
    if (progress < 0 || progress > duration)
    {
        return;
    }
    if (!formatContext)
    {
        return;
    }

    // formatContext 多线程， av_seek_frame内部会对我们的 formatContext上下文的成员做处理，安全的问题
    // 互斥锁 保证多线程情况下安全

    pthread_mutex_lock(&seek_mutex);

    // FFmpeg 大部分单位 == 时间基AV_TIME_BASE
    /**
     * 1.formatContext 安全问题
     * 2.-1 代表默认情况，FFmpeg自动选择 音频 还是 视频 做 seek，  模糊：0视频  1音频
     * 3. AVSEEK_FLAG_ANY（老实） 直接精准到 拖动的位置，问题：如果不是关键帧，B帧 可能会造成 花屏情况
     *    AVSEEK_FLAG_BACKWARD（则优  8的位置 B帧 ， 找附件的关键帧 6，如果找不到他也会花屏）
     *    AVSEEK_FLAG_FRAME 找关键帧（非常不准确，可能会跳的太多），一般不会直接用，但是会配合用
     */
    int r = av_seek_frame(formatContext, -1, progress * AV_TIME_BASE, AVSEEK_FLAG_FRAME);
    if (r < 0)
    {
        // TODO 同学们自己去完成，给Java的回调
        return;
    }

    if (video_channel)
    {
        video_channel->packets.setWork(0); // 队列不工作
        video_channel->frames.setWork(0);  // 队列不工作
        video_channel->packets.clear();
        video_channel->frames.clear();
        video_channel->packets.setWork(1); // 队列继续工作
        video_channel->frames.setWork(1);  // 队列继续工作
    }

    pthread_mutex_unlock(&seek_mutex);
}

void *task_stop(void *args)
{
    auto *player = static_cast<DerryPlayer *>(args);
    player->stop_(player);
    return nullptr;
}

void DerryPlayer::stop_(DerryPlayer *derryPlayer)
{
    isPlaying = false;
    pthread_join(pid_prepare, nullptr);
    pthread_join(pid_start, nullptr);

    // pid_prepare pid_start 就全部停止下来了  稳稳的停下来
    if (formatContext)
    {
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }
    DELETE(video_channel);
    DELETE(derryPlayer);
}

void DerryPlayer::stop()
{

    // 如果是直接释放 我们的 prepare_ start_ 线程，不能暴力释放 ，否则会有bug

    // 让他 稳稳的停下来

    // 我们要等这两个线程 稳稳的停下来后，我再释放DerryPlayer的所以工作
    // 由于我们要等 所以会ANR异常

    // 所以我们我们在开启一个 stop_线程 来等你 稳稳的停下来
    // 创建子线程
    pthread_create(&pid_stop, nullptr, task_stop, this);
}

bool DerryPlayer::if_ready_to_start() 
{
    return ifReady;
}
