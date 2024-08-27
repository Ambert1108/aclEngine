#include <iostream>
#include "acle/videoEngine23_acl.hpp"
#include "acle/core/aclimgtrans.h"

#include "seeker/common.h"

using namespace acle;

void saveFileH264(AVPacket& inPacket, FILE* fileStream) {
  fwrite(inPacket.data, 1, inPacket.size, fileStream);
}

int main(int argc, char* argv[]) {

  I_LOG("start encoder test");
  if (argc != 7) {
    std::cout
      << "Usage <deviceId> <input> <output> <width> <height> <fps>"
      << std::endl;
    exit(0);
  }

  std::string deviceId = argv[1];
  const char* inputFile = argv[2];
  const char* outputH264 = argv[3];
  int64_t width = std::atoi(argv[4]);
  int height = std::atoi(argv[5]);
  int fps = std::stoi(argv[6]);

  Encoder23 encoder;
  Tools23 tool;

  AVFrame* frame = av_frame_alloc();
  AVFrame* hwFrame = av_frame_alloc();
  uint8_t* file_buffer = (uint8_t*)av_malloc(width * height * 3 / 2);
  FILE* in_file = fopen(inputFile, "rb");
  FILE* out_file = fopen(outputH264, "wb");
  AVCodecContext* encodec_ctx = nullptr;
  AVPacket* pkt_ = av_packet_alloc();

  AVBufferRef* hwCtx = nullptr;
  int err = av_hwdevice_ctx_create(&hwCtx, AV_HWDEVICE_TYPE_ASCEND, deviceId.c_str(), NULL, 0);

  encoder.enableHwDevice(hwCtx, AV_PIX_FMT_ASCEND);
  //encoder.rateControlPreset(960000);
  encodec_ctx = encoder.getContext();
  encodec_ctx->width = width;
  encodec_ctx->height = height;
  //encodec_ctx->bit_rate = 1200000;
  encodec_ctx->time_base = { 1 , 30 };
  //encodec_ctx->time_base.den = fps;
  //encodec_ctx->time_base.num = 1;
  encodec_ctx->framerate = { 30 , 1 };
  encodec_ctx->pix_fmt = AV_PIX_FMT_ASCEND;
  //encodec_ctx->gop_size = 12;
  //encodec_ctx->profile = FF_PROFILE_H264_BASELINE;
  //encodec_ctx->level = 31;
  //encodec_ctx->max_b_frames = 0;
  //encodec_ctx->qmin = 10;
  //encodec_ctx->qmax = 51;
  AVDictionary* dict = nullptr;
  av_dict_set_int(&dict, "profile", 0, 0);
  av_dict_set_int(&dict, "rc_mode", 0, 0);
  av_dict_set_int(&dict, "gop", 12, 0);
  av_dict_set_int(&dict, "frame_rate", 30, 0);
  av_dict_set_int(&dict, "max_bit_rate", 8000, 0);
  av_dict_set_int(&dict, "device_id", std::atoi(deviceId.c_str()), 0);

  encoder.open(dict);
  dict = nullptr;

  int frameCount = 1;
  int64_t start = seeker::time::currentTime();
  int64_t use = 0;
  int set = 1;
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
    if (frameCount < 3) {
      frameCount++;
      av_frame_unref(frame);
      continue;
    }
    tool.scale(frame, frame->width, frame->height, AV_PIX_FMT_NV12);
    if (set > 0) {
      frame->pict_type = AV_PICTURE_TYPE_I;
      --set;
    }

    //av_hwframe_transfer_data(hwFrame, frame, 0);
    start = seeker::time::currentTime();
    if (encoder.getPacket(frame, pkt_) != 0) {
      av_frame_unref(frame);
      av_packet_unref(pkt_);
      I_LOG("encode failed");
      continue;
    }
    use = seeker::time::currentTime() - start;
    saveFileH264(*pkt_, out_file);
    av_frame_unref(frame);
    //av_frame_unref(hwFrame);
    av_packet_unref(pkt_);
    I_LOG("encode frame {}", frameCount++);
  }
  I_LOG("encode {} frame use {}ms", frameCount, use);
  av_frame_free(&frame);
  //av_frame_free(&hwFrame);
  av_packet_free(&pkt_);
  encoder.close();
  if (file_buffer) free(file_buffer);
  return 0;
}