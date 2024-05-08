#pragma once
#include "acl/acl.h"
#include "refer/AclLiteUtils.h"
#include "refer/AclLiteError.h"
#include "refer/AclLiteResource.h"
#include "refer/AclLiteVideoProc.h"
#include "refer/AclLiteImageProc.h"
#include "refer/AclLiteVideoCapBase.h"

#include <string>

namespace acle {
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
        //aclLiteVideoProc->Close();
        delete aclLiteVideoProc;
        aclLiteVideoProc = nullptr;
      }
      ACLLITE_LOG_INFO("[Decoder::close] Decoder is closed");
    }

  private:
    AclLiteError OpenVideoCapture() {
      if (IsRtspAddr(streamName)) {
        aclLiteVideoProc = new AclLiteVideoProc(streamName, deviceId);
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

  class Encoder {
  public:
    Encoder(VencConfig& vencConfig, int32_t devId, aclrtContext aclCtx) :
      aclLiteVideoProc(nullptr),
      config(vencConfig),
      deviceId(devId),
      context(aclCtx) {
      ACLLITE_LOG_INFO("[Encoder::Encoder] Encoder is create, devId=%d", devId);
    }
  
    ~Encoder() {
      close();
    }
  
    bool open() {
      if (OpenVideoCapture() != ACLLITE_OK) {
        return false;
      }
  
      return true;
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
        //aclLiteVideoProc->Close();
        delete aclLiteVideoProc;
        aclLiteVideoProc = nullptr;
      }
      ACLLITE_LOG_INFO("[Encoder::close] Encoder is closed");
    }
  
  private:
    AclLiteError OpenVideoCapture() {
      ACLLITE_LOG_INFO("[Encoder:OpenVideoCapture] width=%d, height=%d, outFile=%s, format=%d, enType=%d",
        config.maxWidth, config.maxHeight, config.outFile.c_str(), config.format, config.enType);
      aclLiteVideoProc = new AclLiteVideoProc(config);
      bool res = aclLiteVideoProc->IsOpened();
      if (!res) {
        ACLLITE_LOG_ERROR("[Encoder::OpenVideoCapture] Failed to open venc, res={}", res);
        return ACLLITE_ERROR;
      }
  
      return ACLLITE_OK;
    }
  
    int32_t deviceId;
    aclrtContext context;
    VencConfig config;
    AclLiteVideoProc* aclLiteVideoProc;
  };

  class ImageHandler {
  public:
    ImageHandler() {
      imageProc = new AclLiteImageProc();
    }

    ~ImageHandler() {
      if (imageProc) {
        delete imageProc;
        imageProc = nullptr;
      }
    }

    bool open() {
      AclLiteError ret = imageProc->Init();
      if (ret) {
        ACLLITE_LOG_ERROR("[ImageHandler::open] image handler init failed, error %d", ret);
        return false;
      }

      return true;
    }

    AclLiteError resize(ImageData& src, ImageData& dest, uint32_t width, uint32_t height) {
      if (src.width == width || src.height == height) return ACLLITE_ERROR_DEST_INVALID;
      imageProc->Resize(dest, src, width, height);
      return ACLLITE_OK;
    }

  private:
    AclLiteImageProc* imageProc = nullptr;
  };
}