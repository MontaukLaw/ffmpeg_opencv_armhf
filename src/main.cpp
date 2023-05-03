#include <unistd.h>
#include <iostream>
#include <vector>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}
#include "DerryPlayer.h"
#include "opencv2/opencv.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"

using namespace std;

#define RTMP_ADDR "rtmp://ziku.montauklaw.com:1935/livestream"
DerryPlayer *player;

struct buffer
{
    void *start;
    size_t length;
};
struct buffer *buffers;

void renderCallback(uint8_t *src_data, int width, int height, int src_linesize)
{
    static int counter = 0;
    // player->get_video_channel()->start_detecting();
    // cout << "renderCallback" << endl;
    // cout << "width: " << width << endl;
    // cout << "height: " << height << endl;
    // cout << "src_linesize: " << src_linesize << endl;
    if (counter == 0)
    {
        cv::Mat im = cv::Mat(cv::Size(width, height), CV_8UC3, src_data);
        // cv::imwrite("/tmp/test.jpg", im);
    }
    counter++;
    // 显示数据
    cout << (int)src_data[0] << " " << (int)src_data[1] << " " << (int)src_data[2] << endl;

    // sleep(1);
    // player->get_video_channel()->stop_detecting();
}

int main(int argc, char *argv[])
{
    cout << "start init_decode" << endl;
    player = new DerryPlayer(RTMP_ADDR);
    player->setRenderCallback(renderCallback);
    player->prepare();

    while (player->if_ready_to_start() == false)
    {
        usleep(1000 * 1000);
    }

    player->start();

    getchar();
    delete player;
    // std::cout << "Hello World!" << std::endl;
    return 0;
}