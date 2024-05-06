#pragma once
#include "acl/acl.h"
#include "acllite/AclLiteUtils.h"
#include "acllite/AclLiteUtils.h"
#include "acllite/AclLiteError.h"
#include "acllite/AclLiteResource.h"
#include "acllite/AclLiteImageProc.h"
#include "acllite/AclLiteModel.h"
#include "AclLiteVideoProc.h"
#include "AclLiteVideoCapBase.h"

#include <string>

namespace huawei {

  class Decoder {
  public:
    Decoder(std::string streamName, int32_t devId, aclrtContext aclCtx) :
      aclLiteVideoProc(nullptr),
      streamName(streamName),
      deviceId(devId),
      context(aclCtx) {
      ACLLITE_LOG_INFO("[Decoder::Decoder] Decoder is create, streamName=%s, devId=%d", streamName.c_str(), devId);
    }

    ~Decoder() {
      close();
    }

    AclLiteError open() {
      if (OpenVideoCapture() != ACLLITE_OK) {
        return ACLLITE_ERROR;
      }

      return ACLLITE_OK;
    }

    AclLiteError getFrame(ImageData& data) {
      uint32_t videoWidth_ = aclLiteVideoProc->Get(FRAME_WIDTH);
      uint32_t videoHeight_ = aclLiteVideoProc->Get(FRAME_HEIGHT);
      float fps = aclLiteVideoProc->Get(VIDEO_FPS);


      AclLiteError ret = aclLiteVideoProc->Read(data);
      if (ret == ACLLITE_ERROR_DECODE_FINISH) {
        ACLLITE_LOG_ERROR("[Decoder::getFrame] decoder read frame finish");
      }

      else if (ret != ACLLITE_OK) {
        ACLLITE_LOG_ERROR("[Decoder::getFrame] decoder read frame failed, errCode=%d", ret);
      }
      return ret;
    }

    void close() {
      if (aclLiteVideoProc != nullptr) {
        aclLiteVideoProc->Close();
        delete aclLiteVideoProc;
      }
      ACLLITE_LOG_INFO("[Decoder::close] Decoder is closed");
    }

  private:
    AclLiteError OpenVideoCapture() {
      if (IsRtspAddr(streamName)) {
        aclLiteVideoProc = new AclLiteVideoProc(streamName, deviceId, context);
      }
      else if (IsVideoFile(streamName)) {
        if (!IsPathExist(streamName)) {
          ACLLITE_LOG_ERROR("[Decoder::OpenVideoCapture] The %s is inaccessible", streamName.c_str());
          return ACLLITE_ERROR;
        }
        aclLiteVideoProc = new AclLiteVideoProc(streamName, deviceId);
      }
      else {
        ACLLITE_LOG_ERROR("[Decoder::OpenVideoCapture] Invalid param. The arg should be accessible rtsp,"
          " video file or camera id");
        return ACLLITE_ERROR;
      }

      if (!aclLiteVideoProc->IsOpened()) {
        delete aclLiteVideoProc;
        ACLLITE_LOG_ERROR("[Decoder::IsOpened] Failed to open vdec");
        return ACLLITE_ERROR;
      }

      return ACLLITE_OK;
    }

    std::string streamName;
    int32_t deviceId;
    aclrtContext context;
    AclLiteVideoProc* aclLiteVideoProc;
  };
}