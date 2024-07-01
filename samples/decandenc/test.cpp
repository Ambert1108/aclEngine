#include "aclengine/AclEngine.hpp"
#include "rtpTrs/rtpTrs.hpp"
#include "VideoEngine23.hpp"

#include <iostream>
#include <string>
#include <queue>
#include <deque>
#include <vector>

int run(int num) {
  //设置为偶数
  return num - (num & 1);
}

int run2(int num) {
  //设置为奇数
  return num - ((num & 1) ^ 1);
}

uint32_t SaveOutputFile(const char* fileName, const void* devPtr, uint32_t dataSize) {
  FILE* outFileFp = fopen(fileName, "wb+");
  void* hostPtr = nullptr;
  aclrtMallocHost(&hostPtr, dataSize);
  aclrtMemcpy(hostPtr, dataSize, devPtr, dataSize, ACL_MEMCPY_DEVICE_TO_HOST);
  fwrite(hostPtr, sizeof(char), dataSize, outFileFp);
  (void)aclrtFreeHost(hostPtr);
  fflush(outFileFp);
  fclose(outFileFp);
  return 0;
}

uint32_t AlignmentHelper(uint32_t origSize, uint32_t alignment) {
  if (alignment == 0) {
    return 0;
  }
  uint32_t alignmentH = alignment - 1;
  return (origSize + alignmentH) / alignment * alignment;
}

int main(int argc, char* argv[]) {
  //int argNum = 6;
  //if ((argc < argNum) || (argv[1] == nullptr)) {
  //  std::cout << "Please input: ./test <device_id> <input_file> <outIp> <outPort> <reszie_width> <resize_height>" 
  //    << std::endl;
  //  return ACLLITE_ERROR;
  //}
  //
  //int32_t deviceId = std::atoi(argv[1]);
  //std::string inputName = std::string(argv[2]);
  //std::string ip = std::string(argv[3]);
  //uint16_t port = std::atoi(argv[4]);
  //int destWidth = std::atoi(argv[5]);
  //int destHeight = std::atoi(argv[6]);

  int32_t deviceId = 1;
  std::string inputName = "/home/data/v2.mp4";
  std::string inputImage = "/home/data/cat.jpg";
  std::string ip = "10.4.7.113";
  uint16_t port = 50104;
  int destWidth = 1280;
  int destHeight = 720;

  I_LOG("[main] deviceI={}, input={}, addr={}:{}, destWidth={}, destHeight={}",
    deviceId, inputName, ip, port, destWidth, destHeight);
  using namespace acle;

  Resource aclDev(deviceId, "", false);
  if (aclDev.Init() != ACLE_OK) {
    I_LOG("init acl dev failed");
    return -1;
  }
  
  //Decoder23* decoder23 = new Decoder23(inputName, deviceId, nullptr);
  //if (decoder23->open() != 0) {
  //  return -1;
  //}

  Demuxer* demuxer = new Demuxer();
  if (!demuxer->open(inputName)) {
    return -1;
  }

  Decoder* decoder = nullptr;

  Encoder* encoder = nullptr;
  
  ImageReader reader;
  reader.open();
  ImageHandler imager;
  imager.open();
  AclFrame img = reader.imgread(inputImage);
  //SaveOutputFile("check.yuv", img.data, img.size);

  seeker::rtp::RtpTransceiver::init(8);

  //std::unique_ptr<VideoEngine23::Mux23> muxer = std::make_unique<VideoEngine23::Mux23>();
  std::unique_ptr<Muxer> muxer = std::make_unique<Muxer>();
  std::unique_ptr<seeker::rtp::RtpTransceiver> sender = std::make_unique<seeker::rtp::RtpTransceiver>("huawei", 32);
  sender->open("0.0.0.0", 41200);
  sender->setDestination(ip, port);

  std::queue<std::vector<uint8_t>> rtpBuf = {};
  std::deque<seeker::rtp::Rtp> sendQueue = {};
  SafeQueue<std::shared_ptr<AclPacket>> rpktQue{};

  //设置视频seq, timestamp及timestamp增量，mark值，随机设置ssrc，设置payloadType
  uint16_t seq = 1;
  uint32_t ts = 0;
  uint32_t tsIncrement = 90000 / 25;
  bool mark = false;
  uint32_t ssrc = rand() % 9000000 + 1000000 + (int32_t)seeker::time::currentTime();
  int videoPayloadType = 96;
  int totalFrameNum = 0;
  bool run = true;
  while (run) {
    std::shared_ptr<AclPacket> rpkt = std::make_shared<AclPacket>();
    int ret = demuxer->demux(rpkt);
    if (ret != 0) {
      if (ret == -3) run = false;
      else continue;
    }
    
    AclFrame frame;
    if (!decoder) {
      CodecFormat fmt;
      fmt.width = demuxer->getFileWidth();
      fmt.height = demuxer->getFileHeight();
      aclrtGetCurrentContext(&fmt.context);
      I_LOG("[Decoder:Init] width={}, height={}, format={}, enType={}",
        fmt.width, fmt.height, fmt.format, fmt.enType);
      decoder = new Decoder(fmt);
      if (!decoder->open()) {
        E_LOG("[Decoder::OpenVideoCapture] Failed to open vdec");
        break;
      }
    }
    ret = decoder->readFrame(rpkt, frame);
    if (ret != 0) continue;

    //ImageData frame;
    //AclLiteError ret = decoder23->readFrame(frame);
    //if (ret != ACLLITE_OK) break;
    
    AclFrame newFrame;

    //视频缩放
    //if (imager.resize(frame, newFrame, 320, 180) != ACLLITE_OK) {
    //  newFrame = frame;
    //}
    //I_LOG("frame width={}, height={}", frame.width, frame.height);

    //图片叠加
    imager.overlay(img, frame, 300, 200);
  
    if (!encoder) {
      CodecFormat fmt;
      fmt.width = destWidth;
      fmt.height = destHeight;
      fmt.file = "test.mp4";
      aclrtGetCurrentContext(&fmt.context);
      I_LOG("[Encoder:Init] width={}, height={}, format={}, enType={}",
        fmt.width, fmt.height, fmt.format, fmt.enType);
      encoder = new Encoder(fmt);
      if (!encoder->open()) {
        E_LOG("[Encoder::OpenVideoCapture] Failed to open venc");
        break;
      }
    }
    
    AclPacket pkt;
    ret = encoder->writeFrame(frame, pkt);
    if (ret != 0) {
      if (ret < 0) {
        E_LOG("encoder process failed");
        break;
      }
      continue;
    }

    I_LOG("current pkt size={}", pkt.size);
    //av_packet_unref(pkt);

    if (muxer->mux((uint8_t*)pkt.data, pkt.size, rtpBuf) != 0) {
      E_LOG("muxer failed");
      continue;
    }

    int pktCount = 0;
    int markSize = rtpBuf.size();
    ts += tsIncrement;
    while (!rtpBuf.empty()) {
      seeker::rtp::RawData each = rtpBuf.front();
      pktCount++;
      mark = (pktCount == markSize);
    
      //去除SEI包
      if (int(each.front()) == 6) {
        rtpBuf.pop();
        continue;
      }
    
      seeker::rtp::Rtp rtpPacket{ videoPayloadType, mark, seq++, ts, ssrc, each };
      sendQueue.emplace_back(std::move(rtpPacket));
      rtpBuf.pop();
    }
    while (!sendQueue.empty()) sender->sendRtp(sendQueue);
    totalFrameNum++;
  }

  //if (decoder23) delete decoder23;
  
  if (demuxer) delete demuxer;

  if (decoder != nullptr) {
    I_LOG("start delete decoder");
    delete decoder;
  }
  
  if (encoder != nullptr) {
    I_LOG("start delete encoder"); 
    delete encoder;
  }

  seeker::rtp::RtpTransceiver::shutdown();
  I_LOG("[main] dec and enc test finish, totalNum={}", totalFrameNum);

	return 0;
}