#include "acl/acl.h"
#include "aclengine/refer/AclLiteUtils.h"
#include "aclengine/refer/AclLiteError.h"
#include "aclengine/refer/AclLiteResource.h"
#include "aclengine/refer/AclLiteVideoProc.h"
#include "aclengine/refer/AclLiteImageProc.h"
#include "aclengine/refer/AclLiteVideoCapBase.h"

#include <iostream>
#include <string>

int32_t deviceId = 1;
std::string inputName;
std::string outputName;

void test1() {
  ACLLITE_LOG_INFO("run test 1");
  AclLiteResource aclDev(deviceId, "");
  aclDev.Init();
  AclLiteVideoProc* decoder = new AclLiteVideoProc(inputName, deviceId);
  if (!decoder->IsOpened()) {
    delete decoder;
    ACLLITE_LOG_ERROR("[Decoder::IsOpened] Failed to open vdec");
    return;
  }
  
  AclLiteVideoProc* encoder = nullptr;
  
  bool run = true;
  while (run) {
    ImageData frame;
    AclLiteError ret = decoder->Read(frame);
    if (ret != ACLLITE_OK) {
      break;
    }
  
    if (!encoder) {
      VencConfig vencInfo;
      vencInfo.maxWidth = frame.width;
      vencInfo.maxHeight = frame.height;
      vencInfo.outFile = outputName;
      ACLLITE_LOG_INFO("[Encoder:Init] width=%d, height=%d, outFile=%s, format=%d, enType=%d",
        vencInfo.maxWidth, vencInfo.maxHeight, vencInfo.outFile.c_str(), vencInfo.format, vencInfo.enType);
      encoder = new AclLiteVideoProc(vencInfo);
      if (!encoder->IsOpened()) {
        delete encoder;
        ACLLITE_LOG_ERROR("Failed to open encoder");
        return;
      }
    }
  
    ret = encoder->Read(frame);
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
}


void test2() {
  ACLLITE_LOG_INFO("run test 2");
  AclLiteResource aclDev(deviceId, "");
  aclDev.Init();
  
  AclLiteVideoProc* encoder = nullptr;
  VencConfig vencInfo;
  vencInfo.maxWidth = 1280;
  vencInfo.maxHeight = 720;
  vencInfo.outFile = outputName;
  ACLLITE_LOG_INFO("[Encoder:Init] width=%d, height=%d, outFile=%s, format=%d, enType=%d",
    vencInfo.maxWidth, vencInfo.maxHeight, vencInfo.outFile.c_str(), vencInfo.format, vencInfo.enType);
  encoder = new AclLiteVideoProc(vencInfo);
  if (!encoder->IsOpened()) {
    delete encoder;
    ACLLITE_LOG_ERROR("Failed to open encoder");
    return;
  }
  
  if (encoder != nullptr) {
    encoder->Close();
    delete encoder;
  }
  ACLLITE_LOG_INFO("[Encoder::close] Encoder is closed");
}

int main(int argc, char* argv[]) {
  int argNum = 2;
  if ((argc < argNum) || (argv[1] == nullptr)) {
    std::cout << "Please input: ./test <input_file> <output_file>" << std::endl;
    return ACLLITE_ERROR;
  }

  deviceId = 1;
  inputName = std::string(argv[1]);
  outputName = std::string(argv[2]);
  ACLLITE_LOG_INFO("[main] deviceI=%d, input=%s, output=%s", 
    deviceId, inputName.c_str(), outputName.c_str());

  test2();

  ACLLITE_LOG_INFO("[main] enc test finish");

  return 0;
}