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

  class EncoderOwn {
  public:
    EncoderOwn(CodecFormat& fmt) :
      encodeCtx(fmt) { };

    ~EncoderOwn() {
      close();
    }

    bool open() { 
      outFp_ = fopen(encodeCtx.outFile.c_str(), "wb+");
      if (outFp_ == nullptr) {
        E_LOG("Open file {} failed, error={}",
          encodeCtx.outFile, strerror(errno));
        return ACLLITE_ERROR_OPEN_FILE;
      }
      return initResource(); 
    }

    void close() {
      AclLiteError ret = setFrameConfig(1, 0);
      if (ret != ACLLITE_OK) {
        E_LOG("[AclEngine::Encoder::Error] Set frame config failed, error={}", ret);
        return;
      }

      ret = aclvencSendFrame(vencChannelDesc_, nullptr,
        nullptr, vencFrameConfig_, nullptr);
      if (ret != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] fail to send eos frame, ret={}", ret);
        return;
      }

      isWork.store(false);

      if (!vencFrameConfig_) {
        (void)aclvencDestroyFrameConfig(vencFrameConfig_);
        vencFrameConfig_ = nullptr;
      }

      if (!inputPicDesc_) {
        void* data = acldvppGetPicDescData(inputPicDesc_);
        if (!data) {
          acldvppFree(data);
        }
        acldvppDestroyPicDesc(inputPicDesc_);
      }

      if (vencStream_ != nullptr) {
        aclError ret = aclrtDestroyStream(vencStream_);
        if (ret != ACL_SUCCESS) {
          E_LOG("[AclEngine::Encoder::Error] destroy stream failed, error={}", ret);
        }
        vencStream_ = nullptr;
      }

      if (vencChannelDesc_ != nullptr) {
        aclError aclRet = aclvencDestroyChannel(vencChannelDesc_);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[AclEngine::Encoder::Error] aclvencDestroyChannel failed, aclRet={}", aclRet);
        }
        (void)aclvencDestroyChannelDesc(vencChannelDesc_);
        vencChannelDesc_ = nullptr;
      }
      void* res = nullptr;
      pthread_cancel(threadId_);
      pthread_join(threadId_, &res);

      I_LOG("[Encoder] Encoder is closed");
    }

    int process(const ImageData& input, AclPacket& packet) {
      AclLiteError ret = createInputPicDesc(input);
      if (ret != ACLLITE_OK) {
        E_LOG("[AclEngine::Encoder::Error] fail to create picture description");
        return -1;
      }

      acldvppStreamDesc* outputStreamDesc = nullptr;

      ret = aclvencSendFrame(vencChannelDesc_, inputPicDesc_,
        static_cast<void*>(outputStreamDesc), vencFrameConfig_, (void*)this);
      if (ret != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] encode frame failed, errorCode={}", ret);
        return -1;
      }

      if (pakcetQueue.Empty()) {
        W_LOG("[AclEngine::Encoder::Warn] get packet failed, wait encode process");
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
        E_LOG("Save venc file {} failed, need write {} bytes, "
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
        E_LOG("[AclEngine::Encoder::Error] get encode out data failed");
      }
      else {
        void* data = acldvppGetStreamDescData(output);
        uint32_t size = acldvppGetStreamDescSize(output);
        //aclrtRunMode mode = aclrtRunMode::ACL_HOST;
        //aclrtGetRunMode(&mode);
        //I_LOG("runMode={}", mode);
        data = CopyDataToHost(data, size, ACL_HOST, MEMORY_NORMAL);
        //uint8_t* ptr = (uint8_t*)data;
        //auto data1 = reinterpret_cast<uint8_t*>(data)[0];
        //auto data2 = reinterpret_cast<uint8_t*>(data)[1];
        //I_LOG("[Debug] ptr[0]={}, ptr[1]={}", data1, data2);
        //for (int i = 0; i < 2; i++) {
        //  uint8_t value = *(ptr + i); // 使用指针算术来访问内存
        //  // 处理 value
        //  I_LOG("[Debug] ptr[{}]={}", i, value);
        //}
        AclPacket pkt;
        pkt.data = (uint8_t*)data;
        pkt.size = acldvppGetStreamDescSize(output);
        pkt.timestamp = acldvppGetStreamDescTimestamp(output);
        pkt.eos = acldvppGetStreamDescEos(output);

        EncoderOwn* own = (EncoderOwn*)user;
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
      EncoderOwn* own = (EncoderOwn*)args;
      if (!own->encodeCtx.context) {
        E_LOG("[AclEngine::Encoder::Error] notify use context can not be nullptr!");
        return nullptr;
      }

      aclError ret = aclrtSetCurrentContext(own->encodeCtx.context);
      if (ret != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] set context failed, errorCode={}", static_cast<int32_t>(ret));
        return nullptr;
      }

      while (own->isWork.load()) {
        (void)aclrtProcessReport(1);
      }

      I_LOG("[AclEngine::Encoder] notify callback func close");
      return nullptr;
    }

    AclLiteError createVencChannel() {
      vencChannelDesc_ = aclvencCreateChannelDesc();
      if (vencChannelDesc_ == nullptr) {
        ACLLITE_LOG_ERROR("Create venc channel desc failed");
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
        ACLLITE_LOG_ERROR("fail to create venc channel");
        return ACLLITE_ERROR_CREATE_VENC_CHAN;
      }

      return ACLLITE_OK;
    }

    AclLiteError createFrameConfig() {
      vencFrameConfig_ = aclvencCreateFrameConfig();
      if (vencFrameConfig_ == nullptr) {
        E_LOG("[AclEngine::Encoder::Error] Create frame config failed");
        return ACLLITE_ERROR_VENC_CREATE_FRAME_CONFIG;
      }

      AclLiteError ret = setFrameConfig(0, 1);
      if (ret != ACLLITE_OK) {
        E_LOG("[AclEngine::Encoder::Error] Set frame config failed, error={}", ret);
        return ret;
      }

      return ACLLITE_OK;
    }

    AclLiteError createInputPicDesc(const ImageData& image) {
      inputPicDesc_ = acldvppCreatePicDesc();
      if (inputPicDesc_ == nullptr) {
        E_LOG("[AclEngine::Encoder::Error] Create input pic desc failed");
        return ACLLITE_ERROR_CREATE_PIC_DESC;
      }
      void* inBufferDev_ = nullptr;
      uint32_t inBufferSize_ = image.size;
      auto aclRet = acldvppMalloc(&inBufferDev_, inBufferSize_);
      if (encodeCtx.runMode != ACL_DEVICE) {
        aclRet = aclrtMemcpy(inBufferDev_, inBufferSize_, image.data.get(), image.size, ACL_MEMCPY_HOST_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[AclEngine::Encoder::Error] acl memcpy data to dev failed, image.size={}, ret={}", image.size, aclRet);
          (void)acldvppFree(inBufferDev_);
          inBufferDev_ = nullptr;
          return false;
        }
      }
      else {
        aclRet = aclrtMemcpy(inBufferDev_, inBufferSize_, image.data.get(), image.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
          ACLLITE_LOG_ERROR("[AclEngine::Encoder::Error] acl memcpy data to dev failed, image.size={}, ret={}", image.size, aclRet);
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
      /* 设置是否为结束帧，0：不是，1：是结束帧 <Ambert May-21-2024> */
      aclError ret = aclvencSetFrameConfigEos(vencFrameConfig_, eos);
      if (ret != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] fail to set eos, ret={}", ret);
        return ACLLITE_ERROR_VENC_SET_EOS;
      }

      /* 设置是否强制重新开启I帧，0：不强制，1：强制 <Ambert May-21-2024> */
      ret = aclvencSetFrameConfigForceIFrame(vencFrameConfig_, forceIFrame);
      if (ret != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] fail to set venc ForceIFrame");
        return ACLLITE_ERROR_VENC_SET_IF_FRAME;
      }
    }

    bool initResource() {
      aclError aclRet = aclrtSetCurrentContext(encodeCtx.context);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] Set context for dvpp venc failed, errorCode={}", aclRet);
        return false;
      }

      AclLiteError ret = pthread_create(&threadId_, nullptr,
        notifyCallbackFunc, (void*)this);
      if (ret != ACLLITE_OK) {
        E_LOG("[AclEngine::Encoder::Error] Create notify callback thread failed, errorCode={}", ret);
        return false;
      }

      ret = createVencChannel();
      if (ret != ACLLITE_OK) {
        E_LOG("[AclEngine::Encoder::Error] Create venc channel failed, errorCode={}", ret);
        return false;
      }

      ret = createFrameConfig();
      if (ret != ACLLITE_OK) {
        E_LOG("[AclEngine::Encoder::Error] Create venc frame config failed, errorCode={}", ret);
        return false;
      }

      aclRet = aclrtCreateStream(&vencStream_);
      if (ret != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] Create enc stream failed, errorCode={}", aclRet);
        return false;
      }

      aclRet = aclrtSubscribeReport(threadId_, vencStream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[AclEngine::Encoder::Error] Venc ubscrible report failed, error={}", 
          aclRet);
        return false;
      }


      I_LOG("[AclEngine::Encoder] Init resource success");
      return true;
    }

    FILE* outFp_;
    CodecFormat encodeCtx;
    pthread_t threadId_;
    aclvencChannelDesc* vencChannelDesc_ = nullptr;
    aclvencFrameConfig* vencFrameConfig_ = nullptr;
    acldvppPicDesc* inputPicDesc_ = nullptr;
    aclrtStream vencStream_;
    SafeQueue<AclPacket> pakcetQueue{};
    std::atomic<bool> isWork{ true };
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