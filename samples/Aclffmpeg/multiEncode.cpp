#include "acle/videoEngine23_acl.hpp"
#include "seeker/common.h"

#include <iostream>
#include <vector>
#include <thread>

using namespace acle;
std::string deviceId = "";
int64_t width = 0;
int height = 0;
int fps = 0;
AVBufferRef* hwCtx = nullptr;

void work(std::vector<AVFrame*> vec, int id) {
  I_LOG("encoder thread {} start", id);
  Encoder23 encoder;

  AVFrame* frame = av_frame_alloc();
  AVCodecContext* encodec_ctx = nullptr;
  AVPacket* pkt_ = av_packet_alloc();

  encoder.enableHwDevice(hwCtx, AV_PIX_FMT_ASCEND);
  encodec_ctx = encoder.getContext();
  encodec_ctx->width = width;
  encodec_ctx->height = height;
  encodec_ctx->bit_rate = 1200000;
  encodec_ctx->time_base = { 1 , fps };
  encodec_ctx->time_base.den = fps;
  encodec_ctx->time_base.num = 1;
  encodec_ctx->framerate = { fps , 1 };
  encodec_ctx->pix_fmt = AV_PIX_FMT_ASCEND;
  encodec_ctx->gop_size = 12;
  encodec_ctx->profile = FF_PROFILE_H264_BASELINE;
  encodec_ctx->level = 31;
  encodec_ctx->max_b_frames = 0;
  encodec_ctx->qmin = 10;
  encodec_ctx->qmax = 51;
  AVDictionary* dict = nullptr;
  av_dict_set_int(&dict, "delay", 0, 0);
  av_dict_set_int(&dict, "forced-idr", 1, 0);
  av_dict_set(&dict, "device_id", deviceId.c_str(), '0');
  encoder.open(dict);
  dict = nullptr;

  int frameCount = 1;
  int64_t start = seeker::time::currentTime();
  int64_t use = 0;
  for (int i = 0; i < vec.size(); i++) {
    av_frame_ref(frame, vec.at(i));
    start = seeker::time::currentTime();
    encoder.getPacket(frame, pkt_);
    use = seeker::time::currentTime() - start;
    av_frame_unref(frame);
    av_packet_unref(pkt_);
    I_LOG("[{}] encode frame {}", id, frameCount++);
  }
  I_LOG("[{}] encode {} frame use {}ms", frameCount, use);
  av_frame_free(&frame);
  av_packet_free(&pkt_);
  encoder.close();
}

int main(int argc, char* argv[]) {

  I_LOG("start multi encoder test");
  if (argc != 7) {
    std::cout
      << "Usage <deviceId> <input> <threadCount> <width> <height> <fps>"
      << std::endl;
    exit(0);
  }

  deviceId = argv[1];
  const char* inputFile = argv[2];
  int threadCount = std::atoi(argv[3]);
  width = std::atoi(argv[4]);
  height = std::atoi(argv[5]);
  fps = std::stoi(argv[6]);

  Tools23 tool;

  AVFrame* frame = av_frame_alloc();
  uint8_t* file_buffer = (uint8_t*)av_malloc(width * height * 3 / 2);
  FILE* in_file = fopen(inputFile, "rb");
  int err = av_hwdevice_ctx_create(&hwCtx, AV_HWDEVICE_TYPE_ASCEND, deviceId.c_str(), NULL, 0);
  std::vector<AVFrame*> vec{};
  std::vector<std::thread> threads{};
  for (int i = 0; ; i++) {
    frame->width = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_YUV420P;
    if (fread(file_buffer, 1, width * height * 3 / 2, in_file) <= 0) {
      break;
    }
    else if (feof(in_file)) {
      break;
    }
    frame->data[0] = file_buffer;
    frame->data[1] = file_buffer + width * height;
    frame->data[2] = file_buffer + width * height * 5 / 4;
    frame->linesize[0] = width;
    frame->linesize[1] = width / 2;
    frame->linesize[2] = width / 2;
    tool.scale(frame, frame->width, frame->height, AV_PIX_FMT_NV12);
    vec.push_back(frame);
  }
  
  for (int i = 1; i <= threadCount; i++) {
    threads.emplace_back(&work, vec, i);
  }
  for (auto& each : threads) {
    if (each.joinable()) each.join();
  }


  if (file_buffer) free(file_buffer);
  return 0;
}