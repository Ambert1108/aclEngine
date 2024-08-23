#include "seeker/loggerApi.h"
#include "acle/videoEngine23_acl.hpp"
#include "acle/core/aclimgtrans.h"

#include <iostream>

using namespace acle;

void saveFileYUV(AVFrame& inFrame, FILE* fileStream) {

  int picSize = inFrame.height * inFrame.width;
  int newSize = picSize * 1.5;
  unsigned char* buf = new unsigned char[newSize];
  int a = 0, i;

  D_LOG("--- 2 --- fmt:{}", inFrame.format);
  for (i = 0; i < inFrame.height; i++)
  {
    memcpy(buf + a, inFrame.data[0] + i * inFrame.linesize[0], inFrame.width);
    a += inFrame.width;
  }

  D_LOG("--- 3 ---");
  for (i = 0; i < inFrame.height / 2; i++)
  {
    memcpy(buf + a, inFrame.data[1] + i * inFrame.linesize[1], inFrame.width / 2);
    a += inFrame.width / 2;
  }

  D_LOG("--- 4 ---");
  for (i = 0; i < inFrame.height / 2; i++)
  {
    memcpy(buf + a, inFrame.data[2] + i * inFrame.linesize[2], inFrame.width / 2);
    a += inFrame.width / 2;
  }

  D_LOG("--- 5 ---");
  fwrite(buf, 1, newSize, fileStream);
  delete buf;
  buf = nullptr;

}

int main(int argc, char* argv[]) {

  MxBase::MxInit(); 
  {
    I_LOG("start decoder test");
    if (argc < 4) {
      std::cout
        << "Usage <deviceId> <input> <output>"
        << std::endl;
      exit(0);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* sftFrame = av_frame_alloc();
    Decoder23 deoder;
    Demux23 demuxer;
    Tools23 tool;
    Transfer transfer;

    std::string deviceId = argv[1];
    const char* inputFilename = argv[2];
    const char* outputFilename = argv[3];

    FILE* fyuv = fopen(outputFilename, "wb");
    if (!fyuv)
    {
      throw std::runtime_error("fopen outfile err");
    }

    AVBufferRef* hwCtx = nullptr;
    int err = av_hwdevice_ctx_create(&hwCtx, AV_HWDEVICE_TYPE_ASCEND, deviceId.c_str(), NULL, 0);
    acl::setDevice(atoi(deviceId.c_str()));
    demuxer.init(inputFilename, hwCtx);
    deoder.setCodecContext(demuxer.getContext());
    deoder.open();

    int useCount = 1;
    int framecount = 1;

    int pktPts;
    while (framecount <= 100) {
      int i = demuxer.getPacket(pkt);
      D_LOG("pkt.size:{}", pkt->size);
      if ((i < 0) && (i != -3)) {
        break;
      }
      if ((pkt->size) && (i != 2)) {
        ++useCount;
        pktPts = pkt->pts;
        int j = deoder.getFrame(pkt, frame);
        if (j == 0) {
          I_LOG("weight:{}, height:{}, pkt pts:{}, frame pts:{}, frame:{}",
            frame->width, frame->height, pktPts, frame->pts, framecount);
          framecount++;
          av_hwframe_transfer_data(sftFrame, frame, 0);
          I_LOG("--- 0 ---", sftFrame->format);
          GpuMat gm;
          transfer.gpu_transfer_frame_to_mat(frame, gm);
          I_LOG("--- 1 --- fmt:{}", sftFrame->format);
          tool.scale(sftFrame, sftFrame->width, sftFrame->height, AV_PIX_FMT_YUV420P);
          saveFileYUV(*sftFrame, fyuv);

          D_LOG("--- 6 ---");
        }
        else {
          E_LOG("decode failed");
        }
        av_frame_unref(sftFrame);
        av_frame_unref(frame);
        av_packet_unref(pkt);
      }
    }
    I_LOG("total count:{}, decode count:{}", useCount, framecount);
    deoder.close();
    fclose(fyuv);
  }
  MxBase::MxDeInit();
  
  return 0;
}