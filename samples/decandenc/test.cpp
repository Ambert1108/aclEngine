#include "aclengine/AclEngine.hpp"
#include "rtpTrs/rtpTrs.hpp"
#include "VideoEngine23.hpp"

#include <iostream>
#include <string>
#include <queue>
#include <deque>
#include <vector>

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

  //int32_t deviceId = 1;
  //std::string inputName = "/home/data/v1.mp4";
  //std::string outputName = "out.mp4";
  //int destWidth = 1280;
  //int destHeight = 720;
  int32_t deviceId = 1;
  std::string inputName = "/home/data/v1.mp4";
  std::string ip = "10.4.7.162";
  uint16_t port = 50104;
  int destWidth = 1280;
  int destHeight = 720;
  I_LOG("[main] deviceI={}, input={}, addr={}:{}, destWidth={}, destHeight={}",
    deviceId, inputName, ip, port, destWidth, destHeight);

  AclLiteResource aclDev(deviceId, "");
  if (aclDev.Init() != ACLLITE_OK) {
    I_LOG("init acl dev failed");
    return -1;
  }
  
  using namespace acle;
  Decoder* decoder = new Decoder(inputName, deviceId, nullptr);
  if (decoder->open() != 0) {
    return -1;
  }

  Encoder* encoder = nullptr;
  
  ImageHandler imager;
  imager.open();

  seeker::rtp::RtpTransceiver::init(8);

  std::unique_ptr<VideoEngine23::Mux23> muxer = std::make_unique<VideoEngine23::Mux23>();
  std::unique_ptr<seeker::rtp::RtpTransceiver> sender = std::make_unique<seeker::rtp::RtpTransceiver>("huawei", 32);
  sender->open("0.0.0.0", 41200);
  sender->setDestination(ip, port);

  AVPacket* pkt = av_packet_alloc();
  std::queue<std::vector<uint8_t>> rtpBuf = {};
  std::deque<seeker::rtp::Rtp> sendQueue = {};

  //设置视频seq, timestamp及timestamp增量，mark值，随机设置ssrc，设置payloadType
  uint16_t seq = 1;
  uint32_t ts = 0;
  uint32_t tsIncrement = 90000 / 25;
  bool mark = false;
  uint32_t ssrc = rand() % 9000000 + 1000000 + (int32_t)seeker::time::currentTime();
  int videoPayloadType = 96;

  bool run = true;
  while (run) {
    ImageData frame;
    AclLiteError ret = decoder->getFrame(frame);
    if (ret != ACLLITE_OK) break;
  
    //ImageData newFrame;
    //if (imager.resize(frame, newFrame, destWidth, destHeight) != ACLLITE_OK) {
    //  newFrame = frame;
    //}
  
    if (!encoder) {
      CodecFormat fmt;
      fmt.width = destWidth;
      fmt.height = destHeight;
      //fmt.runMode = ACL_DEVICE;
      fmt.outFile = "test.mp4";
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
    ret = encoder->process(frame, pkt);
    if (ret != 0) {
      if (ret < 0) {
        E_LOG("encoder process failed");
        break;
      }
      continue;
    }

    I_LOG("current pkt size={}", pkt.size);
    //av_packet_unref(pkt);

    if (muxer->mux(pkt.data, pkt.size, rtpBuf) != 0) {
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
  }
  
  if (decoder != nullptr) {
    decoder->close();
    delete decoder;
  }
  
  if (encoder != nullptr) {
    delete encoder;
  }

  seeker::rtp::RtpTransceiver::shutdown();
  ACLLITE_LOG_INFO("[main] dec and enc test finish");

	return 0;
}

//int main(int argc, char* argv[]) {
//  int argNum = 5;
//  if ((argc < argNum) || (argv[1] == nullptr)) {
//    std::cout << "Please input: ./test <device_id> <input_file> <output_file> <reszie_width> <resize_height>" << std::endl;
//    return ACLLITE_ERROR;
//  }
//
//  int32_t deviceId = std::atoi(argv[1]);
//  std::string inputName = std::string(argv[2]);
//  std::string outputName = std::string(argv[3]);
//  int destWidth = std::atoi(argv[4]);
//  int destHeight = std::atoi(argv[5]);
//  ACLLITE_LOG_INFO("[main] deviceI=%d, input=%s, output=%s, destWidth=%d, destHeight=%d", 
//    deviceId, inputName.c_str(), outputName.c_str(), destWidth, destHeight);
//
//
//  AclLiteResource aclDev(deviceId, "");
//  aclDev.Init();
//  AclLiteVideoProc* decoder = new AclLiteVideoProc(inputName, deviceId);
//  if (!decoder->IsOpened()) {
//    delete decoder;
//    ACLLITE_LOG_ERROR("[Decoder::IsOpened] Failed to open vdec");
//    return ACLLITE_ERROR;
//  }
//
//  AclLiteVideoProc* encoder = nullptr;
//  AclLiteImageProc* tool = new AclLiteImageProc();
//  AclLiteError ret = tool->Init();
//  if (ret) {
//    ACLLITE_LOG_ERROR("tool init failed, error %d", ret);
//    return ACLLITE_ERROR;
//  }
//
//  bool run = true;
//  while (run) {
//    ImageData frame;
//    AclLiteError ret = decoder->Read(frame);
//    if (ret != ACLLITE_OK) {
//      break;
//    }
//
//    ImageData newFrame;
//    tool->Resize(newFrame, frame, destWidth, destHeight);
//
//    if (!encoder) {
//      VencConfig vencInfo;
//      vencInfo.maxWidth = destWidth;
//      vencInfo.maxHeight = destHeight;
//      vencInfo.outFile = outputName;
//      ACLLITE_LOG_INFO("[Encoder:Init] width=%d, height=%d, outFile=%s, format=%d, enType=%d",
//        vencInfo.maxWidth, vencInfo.maxHeight, vencInfo.outFile.c_str(), vencInfo.format, vencInfo.enType);
//      encoder = new AclLiteVideoProc(vencInfo);
//      if (!encoder->IsOpened()) {
//        delete encoder;
//        ACLLITE_LOG_ERROR("[Encoder::OpenVideoCapture] Failed to open venc");
//        return ACLLITE_ERROR;
//      }
//    }
//
//    ret = encoder->Read(newFrame);
//    if (ret != ACLLITE_OK) {
//      break;
//    }
//  }
//
//
//  if (decoder != nullptr) {
//    decoder->Close();
//    delete decoder;
//  }
//  ACLLITE_LOG_INFO("[Decoder::close] Decoder is closed");
//
//  if (encoder != nullptr) {
//    encoder->Close();
//    delete encoder;
//  }
//  ACLLITE_LOG_INFO("[Encoder::close] Encoder is closed");
//
//
//  ACLLITE_LOG_INFO("[main] dec and enc test finish");
//
//  return 0;
//}