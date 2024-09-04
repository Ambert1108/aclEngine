#include "seeker/loggerApi.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include "libavutil/avutil.h"
}

#include <acl/acl.h>
#include <acl/acl_rt.h>
#include <acl/dvpp/hi_dvpp.h>
#include <MxBase/E2eInfer/GlobalInit/GlobalInit.h>

#include <iostream>
#include <thread>

std::string deviceId;
char* inputFilename = nullptr;
AVBufferRef* hwCtx = nullptr;

void work() {
  aclrtSetDevice(std::atoi(deviceId.c_str()));
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();

  int err, video_stream_index;
  AVCodec* codec = nullptr;
  AVCodecContext* codecCtx = NULL;
  AVFormatContext* fmtCtx = avformat_alloc_context();
  if ((err = avformat_open_input(&fmtCtx, inputFilename, NULL, NULL)) < 0) {
    throw std::runtime_error("ERROR: avformat_open_input error, open input file.");
  }

  if ((err = avformat_find_stream_info(fmtCtx, NULL)) < 0) {
    throw std::runtime_error("ERROR: avformat_find_stream_info error, find stream information failed.");
  }

  codec = avcodec_find_decoder_by_name("h264_ascend");
  if (!codec) {
    throw std::runtime_error("ERROR: codec find error");
  }

  for (size_t i = 0; i < fmtCtx->nb_streams; i++) {
    if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index = i;
    }
  }

  if (hwCtx == nullptr) throw std::runtime_error("ERROR: hwCtx is nullptr");

  enum AVHWDeviceType type = AV_HWDEVICE_TYPE_ASCEND;
  for (int i = 0;; i++) {
    const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
    if (!config) {
      E_LOG("Decoder {} does not support device type {}.",
        codec->name, av_hwdevice_get_type_name(type));
    }
    if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
      config->device_type == type) break;
  }

  if (!(codecCtx = avcodec_alloc_context3(codec))) {
    throw std::runtime_error("could not allocate video codec context");
  }

  if (avcodec_parameters_to_context(codecCtx, fmtCtx->streams[video_stream_index]->codecpar) < 0) {
    throw std::runtime_error("ERROR: avcodec_parameters_to_context error, add codec param to codec failed.");
  }

  if (hwCtx != nullptr) {
    codecCtx->pix_fmt = AV_PIX_FMT_ASCEND;
    codecCtx->hw_device_ctx = av_buffer_ref(hwCtx);
  }

  if (avcodec_open2(codecCtx, codec, NULL) < 0) {
    throw std::runtime_error("video could not open codec");
  }

  hi_vdec_chn_param param;
  hi_mpi_vdec_get_chn_param(0, &param);
  I_LOG("current vdec param:\n\ttype:{}\n\tdisplay num:{}\n\tdec mode:{}\n\t"
    "out order:{}\n\t", param.type, param.display_frame_num, param.video_param.dec_mode,
    param.video_param.out_order);
  param.display_frame_num = 0;
  param.video_param.out_order = HI_VIDEO_OUT_ORDER_DEC;
  int ret = hi_mpi_vdec_set_chn_param(0, &param);
  if (ret != 0) {
    E_LOG("set vdec chnl failed");
  }
  hi_mpi_vdec_get_chn_param(0, &param);
  I_LOG("current vdec param:\n\ttype:{}\n\tdisplay num:{}\n\tdec mode:{}\n\t"
    "out order:{}\n\t", param.type, param.display_frame_num, param.video_param.dec_mode,
    param.video_param.out_order);

  int useCount = 1;
  int framecount = 1;

  int pktPts;
  while (framecount <= 100) {
    int i = av_read_frame(fmtCtx, pkt);
    if (i < 0) {
      if (i == AVERROR_EOF) {
        D_LOG("WARNING: av_read_frame warn, no more frame to read.");
        break;
      }
      else throw std::runtime_error("ERROR: av_read_frame error");
    }

    if (pkt->stream_index != video_stream_index) {
      av_packet_unref(pkt);
      continue;
    }

    if (pkt->size) {
      ++useCount;
      pktPts = pkt->pts;
      int j = avcodec_send_packet(codecCtx, pkt);
      if (j < 0) {
        E_LOG("Error sending a packet for decoding,ret={}", j);
        av_packet_unref(pkt);
        continue;
      }
      //std::this_thread::sleep_for(std::chrono::milliseconds(20));
      j = avcodec_receive_frame(codecCtx, frame);
      if (j == AVERROR(EAGAIN) || j == AVERROR_EOF) {
        W_LOG("decode prepare");
        av_packet_unref(pkt);
        av_frame_unref(frame);
        continue;
      }
      else if (j < 0) {
        av_packet_unref(pkt);
        av_frame_unref(frame);
        continue;
      }
      if (j == 0) {
        I_LOG("weight:{}, height:{}, pkt pts:{}, frame pts:{}, frame:{}",
          frame->width, frame->height, pktPts, frame->pts, framecount);
        framecount++;

      }
      else {
        E_LOG("decode failed");
      }
      av_frame_unref(frame);
      av_packet_unref(pkt);
    }

    av_packet_unref(pkt);
    av_frame_unref(frame);
  }
  I_LOG("total count:{}, decode count:{}", useCount, framecount);
  if (pkt) {
    av_packet_free(&pkt);
    pkt = nullptr;
  }
  if (frame) {
    av_frame_free(&frame);
    frame = nullptr;
  }
  if (fmtCtx) {
    avformat_close_input(&fmtCtx);
    fmtCtx = nullptr;
  }
  if (codecCtx) {
    avcodec_flush_buffers(codecCtx);
    avcodec_free_context(&codecCtx);
    codecCtx = nullptr;
  }
}

int main(int argc, char* argv[]) {

  MxBase::MxInit();
  {
    I_LOG("start decoder simple test");
    if (argc < 3) {
      std::cout
        << "Usage <deviceId> <input>"
        << std::endl;
      exit(0);
    }

    deviceId = argv[1];
    inputFilename = argv[2];

    int err = av_hwdevice_ctx_create(&hwCtx, AV_HWDEVICE_TYPE_ASCEND, deviceId.c_str(), NULL, 0);

    std::thread t(&work);
    t.join();

  }
  MxBase::MxDeInit();

  return 0;
}