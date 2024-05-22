#pragma once
#include "acl/acl.h"
#include "core/Types.h"
#include "core/SafeQueue.hpp"
#include "refer/AclLiteUtils.h"
#include "refer/AclLiteError.h"
#include "refer/AclLiteResource.h"
#include "refer/AclLiteVideoProc.h"
#include "refer/AclLiteImageProc.h"
#include "refer/AclLiteVideoCapBase.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"

#include <string>
#include <atomic>

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
    Encoder(CodecFormat& fmt) : encodeCtx(fmt) { };

    ~Encoder() { close(); }

    bool open() { 
      if (!encodeCtx.outFile.empty()) {
        outFp_ = fopen(encodeCtx.outFile.c_str(), "wb+");
        if (outFp_ == nullptr) {
          E_LOG("[Encoder::open] Open file {} failed, error={}", encodeCtx.outFile, strerror(errno));
          return ACLLITE_ERROR_OPEN_FILE;
        }
      }
      
      return initResource(); 
    }

    void close() {
      if (!needClose) return;
      needClose = false;

      if (vencFrameConfig_) {
        AclLiteError ret = setFrameConfig(1, 0);
        if (ret != ACLLITE_OK) {
          W_LOG("[Encoder::close] Set frame config failed, error={}", ret);
        }
      }

      if (vencChannelDesc_) {
        AclLiteError ret = aclvencSendFrame(vencChannelDesc_, nullptr,
          nullptr, vencFrameConfig_, nullptr);
        if (ret != ACL_SUCCESS) {
          W_LOG("[Encoder::close] fail to send eos frame, ret={}", ret);
        }
      }

      if (vencFrameConfig_) {
        (void)aclvencDestroyFrameConfig(vencFrameConfig_);
        vencFrameConfig_ = nullptr;
      }

      if (inputPicDesc_) {
        void* data = acldvppGetPicDescData(inputPicDesc_);
        if (!data) {
          acldvppFree(data);
        }
        acldvppDestroyPicDesc(inputPicDesc_);
      }

      if (vencStream_) {
        aclError ret = aclrtDestroyStream(vencStream_);
        if (ret != ACL_SUCCESS) {
          E_LOG("[Encoder::close] destroy stream failed, error={}", ret);
        }
        vencStream_ = nullptr;
      }

      if (vencChannelDesc_) {
        aclError ret = aclvencDestroyChannel(vencChannelDesc_);
        if (ret != ACL_SUCCESS) {
          E_LOG("[Encoder::close] aclvencDestroyChannel failed, aclRet={}", ret);
        }
        (void)aclvencDestroyChannelDesc(vencChannelDesc_);
        vencChannelDesc_ = nullptr;
      }

      if (isWork.load()) {
        isWork.store(false);
        void* res = nullptr;
        pthread_cancel(threadId_);
        pthread_join(threadId_, &res);
      }

      I_LOG("[AclEngine::Encoder] Encoder is closed");
    }

    int process(const ImageData& input, AclPacket& packet) {
      AclLiteError ret = createInputPicDesc(input);
      if (ret != ACLLITE_OK) {
        E_LOG("[Encoder::process] fail to create picture description");
        return -1;
      }

      acldvppStreamDesc* outputStreamDesc = nullptr;

      ret = aclvencSendFrame(vencChannelDesc_, inputPicDesc_,
        static_cast<void*>(outputStreamDesc), vencFrameConfig_, (void*)this);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::process] encode frame failed, errorCode={}", ret);
        return -1;
      }

      if (pakcetQueue.Empty()) {
        W_LOG("[Encoder::process] get packet failed, wait encode process");
        return 1;
      }
      packet = pakcetQueue.Pop();
      return 0;
    }

  private:
    AclLiteError saveVencFile(void* vencData, uint32_t size) {
      AclLiteError atlRet = ACLLITE_OK;
      void* data = vencData;
      if (encodeCtx.runMode == ACL_HOST) {
        data = CopyDataToHost(vencData, size, encodeCtx.runMode, MEMORY_NORMAL);
      }
      size_t ret = fwrite(data, 1, size, outFp_);
      if (ret != size) {
        E_LOG("[Encoder::saveVencFile] Save venc file {} failed, need write {} bytes, "
          "but only write {} bytes, error: {}",
          encodeCtx.outFile, size, ret, strerror(errno));
        atlRet = ACLLITE_ERROR_WRITE_FILE;
      }
      else {
        fflush(outFp_);
      }

      if (encodeCtx.runMode == ACL_HOST) {
        delete[]((uint8_t*)data);
      }

      return atlRet;
    }

    static void callback(acldvppPicDesc* input, acldvppStreamDesc* output, void* user) {
      uint32_t retCode = acldvppGetStreamDescRetCode(output);
      if (retCode != 0) {
        E_LOG("[Encoder::callback] get encode out data failed");
      }
      else {
        void* data = acldvppGetStreamDescData(output);
        uint32_t size = acldvppGetStreamDescSize(output);
        data = CopyDataToHost(data, size, ACL_HOST, MEMORY_NORMAL);
        AclPacket pkt;
        pkt.data = (uint8_t*)data;
        pkt.size = acldvppGetStreamDescSize(output);
        pkt.timestamp = acldvppGetStreamDescTimestamp(output);
        pkt.eos = acldvppGetStreamDescEos(output);

        Encoder* own = (Encoder*)user;
        own->pakcetQueue.Push(pkt);
        //own->saveVencFile(data, size);
      }
      void* data = acldvppGetPicDescData(input);
      if (!data) {
        acldvppFree(data);
      }
      acldvppDestroyPicDesc(input);
    }

    static void* notifyCallbackFunc(void* args) {
      Encoder* own = (Encoder*)args;
      if (!own->encodeCtx.context) {
        E_LOG("[Encoder::notifyCallbackFunc] notify use context can not be nullptr!");
        return nullptr;
      }

      aclError ret = aclrtSetCurrentContext(own->encodeCtx.context);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::notifyCallbackFunc] set context failed, errorCode={}", ret); 
        return nullptr;
      }

      own->isWork.store(true);
      while (own->isWork.load()) {
        (void)aclrtProcessReport(1);
      }

      //I_LOG("[Encoder::notifyCallbackFunc] notify callback thread close");
      return nullptr;
    }

    AclLiteError createVencChannel() {
      vencChannelDesc_ = aclvencCreateChannelDesc();
      if (vencChannelDesc_ == nullptr) {
        E_LOG("[Encoder::createVencChannel] Create venc channel desc failed");
        return ACLLITE_ERROR_CREATE_VENC_CHAN_DESC;
      }
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_THREAD_ID_UINT64, 8, &threadId_);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_CALLBACK_PTR, 8, &callback);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_ENCODE_TYPE_UINT32, 4, &encodeCtx.enType);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_PIXEL_FORMAT_UINT32, 4, &encodeCtx.format);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_PIC_WIDTH_UINT32, 4, &encodeCtx.width);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_PIC_HEIGHT_UINT32, 4, &encodeCtx.height);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_KEY_FRAME_INTERVAL_UINT32, 4, &encodeCtx.gopSize);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_RC_MODE_UINT32, 4, &encodeCtx.rcMode);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACL_VENC_MAX_BITRATE_UINT32, 4, &encodeCtx.maxBitrate);
      aclvencSetChannelDescThreadId(vencChannelDesc_, threadId_);
      aclvencSetChannelDescCallback(vencChannelDesc_, callback);
      aclvencSetChannelDescEnType(vencChannelDesc_, encodeCtx.enType);
      aclvencSetChannelDescPicFormat(vencChannelDesc_, encodeCtx.format);
      aclvencSetChannelDescPicWidth(vencChannelDesc_, encodeCtx.width);
      aclvencSetChannelDescPicHeight(vencChannelDesc_, encodeCtx.height);
      aclvencSetChannelDescKeyFrameInterval(vencChannelDesc_, encodeCtx.gopSize);
      aclvencSetChannelDescRcMode(vencChannelDesc_, encodeCtx.rcMode);
      aclvencSetChannelDescMaxBitRate(vencChannelDesc_, encodeCtx.maxBitrate);

      aclError ret = aclvencCreateChannel(vencChannelDesc_);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::createVencChannel] fail to create venc channel, error={}", ret);
        return ACLLITE_ERROR_CREATE_VENC_CHAN;
      }

      return ACLLITE_OK;
    }

    AclLiteError createFrameConfig() {
      vencFrameConfig_ = aclvencCreateFrameConfig();
      if (vencFrameConfig_ == nullptr) {
        E_LOG("[Encoder::createFrameConfig] Create frame config failed");
        return ACLLITE_ERROR_VENC_CREATE_FRAME_CONFIG;
      }

      AclLiteError ret = setFrameConfig(0, 1);
      if (ret != ACLLITE_OK) {
        E_LOG("[Encoder::createFrameConfig] Set frame config failed, error={}", ret);
        return ret;
      }

      return ACLLITE_OK;
    }

    AclLiteError createInputPicDesc(const ImageData& image) {
      inputPicDesc_ = acldvppCreatePicDesc();
      if (inputPicDesc_ == nullptr) {
        E_LOG("[Encoder::createInputPicDesc] Create input pic desc failed");
        return ACLLITE_ERROR_CREATE_PIC_DESC;
      }
      void* inBufferDev_ = nullptr;
      uint32_t inBufferSize_ = image.size;
      auto aclRet = acldvppMalloc(&inBufferDev_, inBufferSize_);
      if (encodeCtx.runMode != ACL_DEVICE) {
        aclRet = aclrtMemcpy(inBufferDev_, inBufferSize_, image.data.get(), image.size, ACL_MEMCPY_HOST_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[Encoder::createInputPicDesc] acl memcpy data to dev failed, image.size={}, ret={}", image.size, aclRet);
          (void)acldvppFree(inBufferDev_);
          inBufferDev_ = nullptr;
          return false;
        }
      }
      else {
        aclRet = aclrtMemcpy(inBufferDev_, inBufferSize_, image.data.get(), image.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[Encoder::createInputPicDesc] acl memcpy data to dev failed, image.size={}, ret={}", image.size, aclRet);
          (void)acldvppFree(inBufferDev_);
          inBufferDev_ = nullptr;
          return false;
        }
      }
      acldvppSetPicDescFormat(inputPicDesc_, encodeCtx.format);
      acldvppSetPicDescWidth(inputPicDesc_, image.width);
      acldvppSetPicDescHeight(inputPicDesc_, image.height);
      acldvppSetPicDescWidthStride(inputPicDesc_, ALIGN_UP16(image.width));
      acldvppSetPicDescHeightStride(inputPicDesc_, ALIGN_UP2(image.height));
      acldvppSetPicDescData(inputPicDesc_, inBufferDev_);
      acldvppSetPicDescSize(inputPicDesc_, image.size);

      return ACLLITE_OK;
    }

    AclLiteError setFrameConfig(uint8_t eos, uint8_t forceIFrame) {
      if (!vencFrameConfig_) return ACLLITE_ERROR_INVALID_ARGS;
      /* 设置是否为结束帧，0：不是，1：是结束帧 <Ambert May-21-2024> */
      aclError ret = aclvencSetFrameConfigEos(vencFrameConfig_, eos);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::setFrameConfig] fail to set eos, ret={}", ret);
        return ACLLITE_ERROR_VENC_SET_EOS;
      }

      /* 设置是否强制重新开启I帧，0：不强制，1：强制 <Ambert May-21-2024> */
      ret = aclvencSetFrameConfigForceIFrame(vencFrameConfig_, forceIFrame);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::setFrameConfig] fail to set venc ForceIFrame");
        return ACLLITE_ERROR_VENC_SET_IF_FRAME;
      }
    }

    bool initResource() {
      aclError aclRet = aclrtSetCurrentContext(encodeCtx.context);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[Encoder::initResource] Set context for dvpp venc failed, errorCode={}", aclRet);
        return false;
      }

      AclLiteError ret = pthread_create(&threadId_, nullptr,
        notifyCallbackFunc, (void*)this);
      if (ret != ACLLITE_OK) {
        E_LOG("[Encoder::initResource] Create notify callback thread failed, errorCode={}", ret);
        return false;
      }

      ret = createVencChannel();
      if (ret != ACLLITE_OK) {
        E_LOG("[Encoder::initResource] Create venc channel failed, errorCode={}", ret);
        return false;
      }

      ret = createFrameConfig();
      if (ret != ACLLITE_OK) {
        E_LOG("[Encoder::initResource] Create venc frame config failed, errorCode={}", ret);
        return false;
      }

      aclRet = aclrtCreateStream(&vencStream_);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::initResource] Create enc stream failed, errorCode={}", aclRet);
        return false;
      }

      aclRet = aclrtSubscribeReport(threadId_, vencStream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[Encoder::initResource] Venc ubscrible report failed, error={}", aclRet);
        return false;
      }

      needClose = true;
      I_LOG("[Encoder::initResource] init resource success");
      return true;
    }

    FILE* outFp_;
    CodecFormat encodeCtx;
    pthread_t threadId_;
    aclvencChannelDesc* vencChannelDesc_ = nullptr;
    aclvencFrameConfig* vencFrameConfig_ = nullptr;
    acldvppPicDesc* inputPicDesc_ = nullptr;
    aclrtStream vencStream_ = nullptr;
    SafeQueue<AclPacket> pakcetQueue{};
    std::atomic<bool> isWork{ false };
    bool needClose = false;
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