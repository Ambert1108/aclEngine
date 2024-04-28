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

  class Encoder {
  public:
    Encoder(const VencConfig& vencConfig, int32_t devId, aclrtContext aclCtx) :
      aclLiteVideoProc(nullptr),
      config(vencConfig),
      deviceId(devId),
      context(aclCtx) {
      ACLLITE_LOG_INFO("[Encoder::Encoder] Encoder is create, devId=%d", devId);
    }

    ~Encoder() {
      close();
    }

    AclLiteError open() {
      if (OpenVideoCapture() != ACLLITE_OK) {
        return ACLLITE_ERROR;
      }

      return ACLLITE_OK;
    }

    AclLiteError writeFrame(ImageData& data) {
      uint32_t outputImageFmt = aclLiteVideoProc->Get(OUTPUT_IMAGE_FORMAT);

      AclLiteError ret = aclLiteVideoProc->Read(data);
      if (ret != ACLLITE_OK) {
        ACLLITE_LOG_ERROR("[Encoder::writeFrame] encoder write frame failed, errCode=%d", ret);
        return ACLLITE_ERROR;
      }
      return ACLLITE_OK;
    }

    void close() {
      if (aclLiteVideoProc != nullptr) {
        aclLiteVideoProc->Close();
        delete aclLiteVideoProc;
      }
      ACLLITE_LOG_INFO("[Encoder::close] Encoder is closed");
    }

  private:
    AclLiteError OpenVideoCapture() {
      aclLiteVideoProc = new AclLiteVideoProc(config, context);
      if (!aclLiteVideoProc->IsOpened()) {
        delete aclLiteVideoProc;
        ACLLITE_LOG_ERROR("[Encoder::OpenVideoCapture] Failed to open venc");
        return ACLLITE_ERROR;
      }
      aclLiteVideoProc->Set(VIDEO_FPS, 30);

      return ACLLITE_OK;
    }

    int32_t deviceId;
    aclrtContext context;
    VencConfig config;
    AclLiteVideoProc* aclLiteVideoProc;
  };
}