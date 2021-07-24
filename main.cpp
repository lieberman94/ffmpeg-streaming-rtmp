#include <iostream>
#include <fstream>
#include <unistd.h>
#include "include/clipp.h"
#include <set>

extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/time.h"
#include <libavutil/opt.h>

}

using namespace std;
using namespace clipp; using std::cout; using std::string;
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")

int avError(int errNum);

int push(char inUrls[][300], char *outUrl, bool debug);

int max(int a, int b);

int main(int argc, char *argv[]) {
    std::string input_mp4_file_s = "/Users/leeberli/Downloads/playlist.txt";
    std::string outUrl_s = "rtmp://localhost/live/livestream";
    bool debug = false;

    // 解析参数
    auto cli = (value("mp4 files", input_mp4_file_s),
            value("rtmp url", outUrl_s),
            option("-d", "--debug").set(debug));
    parse(argc, const_cast<char **>(argv), cli);

    char input_mp4_file[input_mp4_file_s.length() + 1];
    char outUrl[outUrl_s.length() + 1];
    strcpy(input_mp4_file, input_mp4_file_s.c_str());
    strcpy(outUrl, outUrl_s.c_str());

    // 解析视频列表文件
    int counter = 0;
    char inUrls[1000][300];
    ifstream inFile(input_mp4_file, ifstream::in);
    if (inFile.good()) {
        while (!inFile.eof() && (counter < 1000)) {
            inFile.getline(inUrls[counter], 300);
            counter++;
        }
    }

    push(inUrls, outUrl, debug);

    return 0;
}

int push(char inUrls[][300], char *outUrl, bool debug) {


    av_register_all();
    avformat_network_init();
    AVFormatContext *octx = nullptr;

    bool first = true;
    int his_pts = 0, his_dts = 0;
    int pkt_pts = 0, pkt_dts = 0;
    int retry_send = 0;
    int retry_send_file = 0;
    std::set<string> error_urls{};

    char *inUrl;
    string inUrl_s;
    for (int ii = 0; ii < 1000; ii++) {
        inUrl = inUrls[ii];
        inUrl_s = inUrl;
        if (error_urls.find(inUrl_s) != error_urls.end()) {
            cout << inUrl << " pass" << endl;
            break;
        }

        // 构建输入context 和 steams
        AVFormatContext *ictx = nullptr;
        int ret = avformat_open_input(&ictx, inUrl, NULL, NULL);
        if (ret < 0) { return avError(ret); }
        if (debug) { cout << "avformat_open_input success!" << endl; }
        ret = avformat_find_stream_info(ictx, 0);
        if (ret != 0) { return avError(ret); }
        if (debug) { av_dump_format(ictx, 0, inUrl, 0); }
        cout << inUrl << " start" << endl;

        // 构建输出context 和 steams
        if (first) {
            ret = avformat_alloc_output_context2(&octx, NULL, "flv", outUrl);
            if (ret < 0) { return avError(ret); }
            if (debug) { cout << "avformat_alloc_output_context2 success!" << endl; }
        }
        for (int i = 0; i < ictx->nb_streams; i++) {
            AVStream *in_stream = ictx->streams[i];
            if (first) {
                AVStream *out_stream = avformat_new_stream(octx, in_stream->codec->codec);
                if (!out_stream) {
                    printf("Failed to add audio and video stream \n");
                    ret = AVERROR_UNKNOWN;
                }
                ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
                if (ret < 0) {
                    printf("copy codec context failed \n");
                }
                out_stream->codecpar->codec_tag = 0;
                out_stream->codec->codec_tag = 0;
                if (octx->oformat->flags & AVFMT_GLOBALHEADER) {
                    out_stream->codec->flags = out_stream->codec->flags | 0;
                }
            }
        }
        if (debug) { av_dump_format(octx, 0, outUrl, 1); }
        int videoindex = -1;
        for (int i = 0; i < ictx->nb_streams; i++) {
            if (ictx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoindex = i;
                break;
            }
        }

        // 打开远程文件,设置header
        if (first) {
            ret = avio_open(&octx->pb, outUrl, AVIO_FLAG_WRITE);
            if (ret < 0) { avError(ret); }
            ret = avformat_write_header(octx, 0);
            if (ret < 0) { avError(ret); }
            if (debug) { cout << "avformat_write_header Success!" << endl; }
        }

        // packet 循环
        AVPacket pkt;
        long long start_time = av_gettime();
        long long frame_index = 0;
        while (1) {
            AVStream *in_stream, *out_stream;
            // 读取packet
            ret = av_read_frame(ictx, &pkt);
            if (ret < 0) {
                cout << inUrl << " done" << endl;
                break;
            }

            if (pkt.pts == AV_NOPTS_VALUE) {
                if (debug) { cout << "Get pre-decode data AV_NOPTS_VALUE!" << endl; }
                //AVRational time_base: time base. This value can be used to convert PTS and DTS into real time.
                AVRational time_base1 = ictx->streams[videoindex]->time_base;
                int64_t calc_duration = (double) AV_TIME_BASE / av_q2d(ictx->streams[videoindex]->r_frame_rate);
                pkt.pts = (double) (frame_index * calc_duration) / (double) (av_q2d(time_base1) * AV_TIME_BASE);
                pkt.dts = pkt.pts;
                pkt.duration = (double) calc_duration / (double) (av_q2d(time_base1) * AV_TIME_BASE);
            }
            // 推的太快了 等待
            if (pkt.stream_index == videoindex) {
                AVRational time_base = ictx->streams[videoindex]->time_base;
                AVRational time_base_q = {1, AV_TIME_BASE};
                int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
                int64_t now_time = av_gettime() - start_time;
                AVRational avr = ictx->streams[videoindex]->time_base;
                if (pts_time > now_time) { av_usleep((unsigned int) (pts_time - now_time)); }
            }

            // copy 输入输出流
            in_stream = ictx->streams[pkt.stream_index];
            out_stream = octx->streams[pkt.stream_index];
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                       (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX))
                      + his_pts;
            pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                       (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX))
                      + his_dts;
            pkt.duration = (int) av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            if (pkt.pts < his_pts) { continue; } // 跳过文件切换的时候前几帧
            pkt_pts = max(pkt.pts, pkt_pts);
            pkt_dts = max(pkt.dts, pkt_dts);
            if (pkt.stream_index == videoindex) { frame_index++; }

            // 实际写 在网络不好时直接跳过
            ret = av_interleaved_write_frame(octx, &pkt);
            if (ret < 0) {
                if (debug) { printf("send packet error "); }
                av_usleep(300000); //等待300ms
                retry_send += 1;
                if (retry_send > 20) { // 重试20次后退出该视频
                    error_urls.insert(inUrl_s);
                    cout << endl << inUrl << " !!! may be wrong !!!" << endl;
                    retry_send = 0;
                    retry_send_file += 1;
                    break;
                }
                if (retry_send_file > 10) { // 重试10次后退出该视频
                    cout << endl << " retry toomany times, exit !!!" << endl;
                    exit(1);
                }
            }
            av_packet_unref(&pkt);
        }

        avformat_free_context(ictx);
        first = false;
        his_pts = pkt_pts;
        his_dts = pkt_dts;
    }
}

int avError(int errNum) {
    char buf[1024];
    av_strerror(errNum, buf, sizeof(buf));
    cout << " failed! " << buf << endl;
    return -1;
}

int max(int a, int b) {
    if (a > b) return a;
    return b;
}