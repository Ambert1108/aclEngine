#pragma once
#include "acl/acl.h"
#include "acllite/AclLiteUtils.h"
#include "acllite/AclLiteError.h"
#include "acllite/AclLiteResource.h"
#include "acllite/AclLiteImageProc.h"
#include "AclLiteVideoProc.h"
#include "AclLiteVideoCapBase.h"

#include <string>

namespace huawei {

  class Decoder {
  public:
    Decoder(std::string streamName) : 
      aclLiteVideoProc(nullptr),
      streamName(streamName) {
      ACLLITE_LOG_INFO("Decoder is create, streamName=%s", streamName.c_str());
    }

    ~Decoder() {
      close();
    }

    AclLiteError InitResource() {
      AclLiteError ret = aclDev.Init();
      if (ret) {
        ACLLITE_LOG_ERROR("Init resource failed, error %d", ret);
        return ACLLITE_ERROR;
      }

      if (OpenVideoCapture() != ACLLITE_OK) {
        return ACLLITE_ERROR;
      }

      ret = aclLiteImgProc.Init();
      if (ret) {
        ACLLITE_LOG_ERROR("Dvpp init failed, error %d", ret);
        return ACLLITE_ERROR;
      }

      runMode = aclDev.GetRunMode();

      return ACLLITE_OK;
    }

    AclLiteError getFrame(ImageData& data) {
      uint32_t videoWidth_ = aclLiteVideoProc->Get(FRAME_WIDTH);
      uint32_t videoHeight_ = aclLiteVideoProc->Get(FRAME_HEIGHT);
      float fps = aclLiteVideoProc->Get(VIDEO_FPS);
      ACLLITE_LOG_INFO("videoWidth=%d, videoHeight=%d, fps=%d", videoWidth_, videoHeight_, fps);

      AclLiteError ret = aclLiteVideoProc->Read(data);
      if (ret != ACLLITE_OK) {
        ACLLITE_LOG_ERROR("acl lite video process read frame failed, errCode=%d", ret);
      }
      return ACLLITE_OK;
    }

    void close() {
      if (aclLiteVideoProc != nullptr) {
        aclLiteVideoProc->Close();
        delete aclLiteVideoProc;
      }
      aclLiteImgProc.DestroyResource();
    }

  private:
    AclLiteError OpenVideoCapture() {
      if (IsRtspAddr(streamName)) {
        aclLiteVideoProc = new AclLiteVideoProc(streamName);
      }
      else if (IsVideoFile(streamName)) {
        if (!IsPathExist(streamName)) {
          ACLLITE_LOG_ERROR("The %s is inaccessible", streamName.c_str());
          return ACLLITE_ERROR;
        }
        aclLiteVideoProc = new AclLiteVideoProc(streamName);
      }
      else {
        ACLLITE_LOG_ERROR("Invalid param. The arg should be accessible rtsp,"
          " video file or camera id");
        return ACLLITE_ERROR;
      }

      if (!aclLiteVideoProc->IsOpened()) {
        delete aclLiteVideoProc;
        ACLLITE_LOG_ERROR("Failed to open video");
        return ACLLITE_ERROR;
      }

      return ACLLITE_OK;
    }

    std::string streamName;
    aclrtRunMode runMode;
    AclLiteResource aclDev;
    AclLiteImageProc aclLiteImgProc;
    AclLiteVideoProc* aclLiteVideoProc;
  };
}