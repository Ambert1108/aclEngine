#include "decoder.hpp"
#include "encoder.hpp"
#include "tool.hpp"

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
//  AclLiteResource* aclDev = new AclLiteResource(deviceId, "");
//  aclDev->Init();
//
//  VencConfig vencInfo;
//  vencInfo.maxWidth = destWidth;
//  vencInfo.maxHeight = destHeight;
//  vencInfo.outFile = outputName;
//
//  using namespace huawei;
//  Decoder decoder(inputName, deviceId, nullptr);
//  if (decoder.open() != 0) {
//    if (aclDev) delete aclDev;
//    return -1;
//  }
//
//  Encoder encoder(vencInfo, deviceId, nullptr);
//  if(encoder.open() != 0) {
//    if (aclDev) delete aclDev;
//    return -1;
//  }
//
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
//    if (decoder.getFrame(frame) != 0) break;
//    ImageData newFrame;
//    tool->Resize(newFrame, frame, destWidth, destHeight);
//    if (encoder.writeFrame(newFrame) != 0) break;
//  }
//
//  if (aclDev) delete aclDev;
//
//  ACLLITE_LOG_INFO("[main] dec and enc test finish");
//
//	return 0;
//}

int main(int argc, char* argv[]) {
  int argNum = 5;
  if ((argc < argNum) || (argv[1] == nullptr)) {
    std::cout << "Please input: ./test <device_id> <input_file> <output_file> <reszie_width> <resize_height>" << std::endl;
    return ACLLITE_ERROR;
  }

  int32_t deviceId = std::atoi(argv[1]);
  std::string inputName = std::string(argv[2]);
  std::string outputName = std::string(argv[3]);
  int destWidth = std::atoi(argv[4]);
  int destHeight = std::atoi(argv[5]);
  ACLLITE_LOG_INFO("[main] deviceI=%d, input=%s, output=%s, destWidth=%d, destHeight=%d", 
    deviceId, inputName.c_str(), outputName.c_str(), destWidth, destHeight);


  AclLiteResource aclDev(deviceId, "");
  aclDev.Init();
  AclLiteVideoProc* decoder = new AclLiteVideoProc(inputName, deviceId);
  if (!decoder->IsOpened()) {
    delete decoder;
    ACLLITE_LOG_ERROR("[Decoder::IsOpened] Failed to open vdec");
    return ACLLITE_ERROR;
  }

  AclLiteVideoProc* encoder = nullptr;
  AclLiteImageProc* tool = new AclLiteImageProc();
  AclLiteError ret = tool->Init();
  if (ret) {
    ACLLITE_LOG_ERROR("tool init failed, error %d", ret);
    return ACLLITE_ERROR;
  }

  bool run = true;
  while (run) {
    ImageData frame;
    AclLiteError ret = decoder->Read(frame);
    if (ret != ACLLITE_OK) {
      break;
    }

    ImageData newFrame;
    tool->Resize(newFrame, frame, destWidth, destHeight);

    if (!encoder) {
      VencConfig vencInfo;
      vencInfo.maxWidth = destWidth;
      vencInfo.maxHeight = destHeight;
      vencInfo.outFile = outputName;
      ACLLITE_LOG_INFO("[Encoder:Init] width=%d, height=%d, outFile=%s, format=%d, enType=%d",
        vencInfo.maxWidth, vencInfo.maxHeight, vencInfo.outFile.c_str(), vencInfo.format, vencInfo.enType);
      encoder = new AclLiteVideoProc(vencInfo);
      if (!encoder->IsOpened()) {
        delete encoder;
        ACLLITE_LOG_ERROR("[Encoder::OpenVideoCapture] Failed to open venc");
        return ACLLITE_ERROR;
      }
    }

    ret = encoder->Read(newFrame);
    if (ret != ACLLITE_OK) {
      break;
    }
  }


  if (decoder != nullptr) {
    decoder->Close();
    delete decoder;
  }
  ACLLITE_LOG_INFO("[Decoder::close] Decoder is closed");

  if (encoder != nullptr) {
    encoder->Close();
    delete encoder;
  }
  ACLLITE_LOG_INFO("[Encoder::close] Encoder is closed");


  ACLLITE_LOG_INFO("[main] dec and enc test finish");

  return 0;
}