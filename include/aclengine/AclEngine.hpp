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

    AclLiteError readFrame(ImageData& data) {
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

    int writeFrame(const ImageData& input, AclPacket& packet) {
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

  static std::unordered_map<std::string, acldvppChannelMode> STR2MODE = {
        {"DVPP_CHNMODE_VPC", DVPP_CHNMODE_VPC},
        {"DVPP_CHNMODE_JPEGD", DVPP_CHNMODE_JPEGD},
        {"DVPP_CHNMODE_JPEGE", DVPP_CHNMODE_JPEGE},
        {"DVPP_CHNMODE_PNGD", DVPP_CHNMODE_PNGD}
  };

  class ImageHandler {
  public:
    ImageHandler() { }

    ~ImageHandler() {
      close();
    }

    bool open(std::string mode = "") {
      aclError aclRet = aclrtCreateStream(&stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::open] Create venc stream failed, error={}", aclRet);
        return ACLLITE_ERROR_CREATE_STREAM;
      }

      channelDesc = acldvppCreateChannelDesc();
      if (channelDesc == nullptr) {
        E_LOG("[ImageHandler::open] Create dvpp channel desc failed");
        return ACLLITE_ERROR_CREATE_DVPP_CHANNEL_DESC;
      }

      auto socVersion = aclrtGetSocName();
      if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0 && mode != "") {
        //mode: 指定通道描述信息中的通道模式，明确图片数据处理通道用于实现哪种功能，目前支持VPC、JPEGD、JPEGE、PNGD功能
        aclRet = acldvppSetChannelDescMode(channelDesc, STR2MODE[mode]);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::open] acldvppCreateChannel failed, aclRet={}", aclRet);
          return ACLLITE_ERRROR_CREATE_DVPP_CHANNEL;
        }
      }

      aclRet = acldvppCreateChannel(channelDesc);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::open] acldvppCreateChannel failed, aclRet={}", aclRet);
        return ACLLITE_ERRROR_CREATE_DVPP_CHANNEL;
      }

      I_LOG("[ImageHandler::open] init resource success");

      return ACLLITE_OK;
    }

    void close() {
      if (isClose) return;
      aclError aclRet;
      if (channelDesc != nullptr) {
        aclRet = acldvppDestroyChannel(channelDesc);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::close] Destroy dvpp channel error={}", aclRet);
        }
        (void)acldvppDestroyChannelDesc(channelDesc);
        channelDesc = nullptr;
      }

      if (stream_ != nullptr) {
        aclRet = aclrtDestroyStream(stream_);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::close] Vdec destroy stream failed, error={}", aclRet);
        }
        stream_ = nullptr;
      }

      isClose = true;
    }

    AclLiteError resize(ImageData& src, ImageData& dest, uint32_t width, uint32_t height) {
      if (src.width == width || src.height == height) {
        E_LOG("[ImageHandler::resize] src width={}, height={} equal to target size", src.width, src.height);
        return ACLLITE_ERROR_DEST_INVALID;
      }
      size_.width = width;
      size_.height = height;
      AclLiteError ret = resizeExecute(src, dest);
      return ret;
    }

    AclLiteError overlay(ImageData& src, ImageData& dest,
      uint32_t targetX, uint32_t targetY) {
      AclLiteError ret = cropAndPaste(src, dest, targetX, targetY);
      return ret;
    }

  private:
    //vpc资源通用释放接口
    void destoryResource() {
      if (inputPicDesc != nullptr) {
        (void)acldvppDestroyPicDesc(inputPicDesc);
        inputPicDesc = nullptr;
      }

      if (outputPicDesc != nullptr) {
        (void)acldvppDestroyPicDesc(outputPicDesc);
        outputPicDesc = nullptr;
      }
    }

    //缩放功能实现
    AclLiteError initResizeInputDesc(ImageData& inputImage) {
      uint32_t alignWidth = inputImage.alignWidth;
      uint32_t alignHeight = inputImage.alignHeight;
      if (alignWidth == 0 || alignHeight == 0) {
        E_LOG("[ImageHandler::initResizeInputDesc] Input image width={} or height={} invalid",
          inputImage.width, inputImage.height);
        return ACLLITE_ERROR_INVALID_ARGS;
      }

      uint32_t inputBufferSize = 0;
      if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_420 || inputImage.format == PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
        inputBufferSize = YUV420SP_SIZE(alignWidth, alignHeight);
        inputImage.width = ALIGN_UP2(inputImage.width);
        inputImage.height = ALIGN_UP2(inputImage.height);
      }
      else if (inputImage.format == PIXEL_FORMAT_RGB_888 || inputImage.format == PIXEL_FORMAT_BGR_888) {
        inputBufferSize = RGBU8_IMAGE_SIZE(alignWidth, alignHeight);
      }
      else {
        W_LOG("[ImageHandler::initResizeInputDesc] Dvpp only support yuv and rgb format.");
      }

      inputPicDesc = acldvppCreatePicDesc();
      if (!inputPicDesc) {
        E_LOG("[ImageHandler::initResizeInputDesc] Create dvpp pic desc failed");
        return ACLLITE_ERROR_CREATE_PIC_DESC;
      }

      acldvppSetPicDescData(inputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(inputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputBufferSize);

      return ACLLITE_OK;
    }

    AclLiteError initResizeOutputDesc() {
      auto socVersion = aclrtGetSocName();
      char soc_name[11];
      strncpy(soc_name, socVersion, 10);
      int resizeOutWidth = ALIGN_UP2(size_.width);
      int resizeOutHeight = ALIGN_UP2(size_.height);
      int resizeOutWidthStride = resizeOutWidth;
      if (strncmp(soc_name, "Ascend310B", sizeof("Ascend310B") - 1) == 0) {
        int resizeOutWidthStride = resizeOutWidth;
      }
      else {
        int resizeOutWidthStride = ALIGN_UP16(resizeOutWidth);
      }
      int resizeOutHeightStride = ALIGN_UP2(resizeOutHeight);
      if (resizeOutWidthStride == 0 || resizeOutHeightStride == 0) {
        E_LOG("[ImageHandler::initResizeOutputDesc] Align resize width({}) and height({}) failed",
          size_.width, size_.height);
        return ACLLITE_ERROR_INVALID_ARGS;
      }

      outDevBufSize = YUV420SP_SIZE(resizeOutWidthStride, resizeOutHeightStride);
      aclError aclRet = acldvppMalloc(&outDevBuf, outDevBufSize);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::initResizeOutputDesc] Dvpp resize malloc output buffer failed, "
          "size={}, error={}", outDevBufSize, aclRet);
        return ACLLITE_ERROR_MALLOC_DVPP;
      }

      outputPicDesc = acldvppCreatePicDesc();
      if (!outputPicDesc) {
        E_LOG("[ImageHandler::initResizeOutputDesc] acldvppCreatePicDesc vpcOutputDesc_ failed");
        return ACLLITE_ERROR_CREATE_PIC_DESC;
      }

      acldvppSetPicDescData(outputPicDesc, outDevBuf);
      acldvppSetPicDescFormat(outputPicDesc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
      acldvppSetPicDescWidth(outputPicDesc, resizeOutWidth);
      acldvppSetPicDescHeight(outputPicDesc, resizeOutHeight);
      acldvppSetPicDescWidthStride(outputPicDesc, resizeOutWidthStride);
      acldvppSetPicDescHeightStride(outputPicDesc, resizeOutHeightStride);
      acldvppSetPicDescSize(outputPicDesc, outDevBufSize);

      return ACLLITE_OK;
    }

    AclLiteError initResizeResource(ImageData& inputImage) {
      resizeConfig_ = acldvppCreateResizeConfig();
      if (resizeConfig_ == nullptr) {
        E_LOG("[ImageHandler::initResizeResource] Dvpp resize init failed for create config failed");
        return ACLLITE_ERROR_CREATE_RESIZE_CONFIG;
      }

      AclLiteError ret = initResizeInputDesc(inputImage);
      if (ret != ACLLITE_OK) {
        E_LOG("[ImageHandler::initResizeResource] InitResizeInputDesc failed");
        return ret;
      }

      ret = initResizeOutputDesc();
      if (ret != ACLLITE_OK) {
        E_LOG("[ImageHandler::initResizeResource] InitResizeOutputDesc failed");
        return ret;
      }

      return ACLLITE_OK;
    }

    void destroyResizeResource() {
      if (resizeConfig_ != nullptr) {
        (void)acldvppDestroyResizeConfig(resizeConfig_);
        resizeConfig_ = nullptr;
      }

      destoryResource();
    }

    AclLiteError resizeExecute(ImageData& srcImage, ImageData& resizedImage) {
      AclLiteError atlRet = initResizeResource(srcImage);
      if (atlRet != ACLLITE_OK) {
        ACLLITE_LOG_ERROR("Dvpp resize failed for init error");
        return atlRet;
      }

      // resize pic
      aclError aclRet = acldvppVpcResizeAsync(channelDesc, inputPicDesc,
        outputPicDesc, resizeConfig_, stream_);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("acldvppVpcResizeAsync failed, error: %d", aclRet);
        return ACLLITE_ERROR_RESIZE_ASYNC;
      }

      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("resize aclrtSynchronizeStream failed, error: %d", aclRet);
        return ACLLITE_ERROR_SYNC_STREAM;
      }
      resizedImage.format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
      char soc_name[11];
      auto socVersion = aclrtGetSocName();
      strncpy(soc_name, socVersion, 10);
      if (strncmp(soc_name, "Ascend310B", sizeof("Ascend310B") - 1) == 0) {
        resizedImage.width = ALIGN_UP2(size_.width);
        resizedImage.height = ALIGN_UP2(size_.height);
        resizedImage.alignWidth = size_.width;
        resizedImage.alignHeight = ALIGN_UP2(size_.height);
      }
      else {
        resizedImage.width = size_.width;
        resizedImage.height = size_.height;
        resizedImage.alignWidth = ALIGN_UP16(size_.width);
        resizedImage.alignHeight = ALIGN_UP2(size_.height);
      }
      resizedImage.size = outDevBufSize;
      resizedImage.data = SHARED_PTR_DVPP_BUF(outDevBuf);

      destroyResizeResource();

      return ACLLITE_OK;
    }

    //裁剪并叠加功能实现
    AclLiteError initCropAndPasteInputDesc(ImageData& inputImage) {
      originalImageWidth_ = inputImage.width;
      originalImageHeight_ = inputImage.height;
      uint32_t alignWidth = inputImage.alignWidth;
      uint32_t alignHeight = inputImage.alignHeight;
      if (alignWidth == 0 || alignHeight == 0) {
        ACLLITE_LOG_ERROR("Invalid image parameters, width %d, height %d",
          inputImage.width, inputImage.height);
        return ACLLITE_ERROR;
      }

      uint32_t inputBufferSize = 0;
      if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
        inputBufferSize = YUV420SP_SIZE(alignWidth, alignHeight);
      }
      else if (inputImage.format == PIXEL_FORMAT_RGB_888) {
        inputBufferSize = RGBU8_IMAGE_SIZE(alignWidth, alignHeight);
      }
      else {
        ACLLITE_LOG_WARNING("Dvpp only support yuv and rgb format.");
      }

      inputPicDesc = acldvppCreatePicDesc();
      if (inputPicDesc == nullptr) {
        ACLLITE_LOG_ERROR("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
      }

      acldvppSetPicDescData(inputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(inputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputBufferSize);

      return ACLLITE_OK;
    }

    AclLiteError initCropAndPasteOutputDesc() {
      int cropOutWidth = overlayOutSize_.width;
      int cropOutHeight = overlayOutSize_.height;
      int cropOutWidthStride = ALIGN_UP16(cropOutWidth);
      int cropOutHeightStride = ALIGN_UP2(cropOutHeight);
      if (cropOutWidthStride == 0 || cropOutHeightStride == 0) {
        ACLLITE_LOG_ERROR("Crop image align widht(%d) and height(%d) failed",
          overlayOutSize_.width, overlayOutSize_.height);
        return ACLLITE_ERROR;
      }

      outDevBufSize = YUV420SP_SIZE(cropOutWidthStride,
        cropOutHeightStride);
      aclError aclRet = acldvppMalloc(&outDevBuf, outDevBufSize);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("Dvpp crop malloc output memory failed, crop "
          "width %d, height %d size %d, error %d",
          overlayOutSize_.width, overlayOutSize_.height,
          outDevBufSize, aclRet);
        return ACLLITE_ERROR;
      }
      aclrtMemset(outDevBuf, outDevBufSize, 0, outDevBufSize);
      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        ACLLITE_LOG_ERROR("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
      }
      acldvppSetPicDescData(outputPicDesc, outDevBuf);
      acldvppSetPicDescFormat(outputPicDesc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
      acldvppSetPicDescWidth(outputPicDesc, cropOutWidth);
      acldvppSetPicDescHeight(outputPicDesc, cropOutHeight);
      acldvppSetPicDescWidthStride(outputPicDesc, cropOutWidthStride);
      acldvppSetPicDescHeightStride(outputPicDesc, cropOutHeightStride);
      acldvppSetPicDescSize(outputPicDesc, outDevBufSize);

      return ACLLITE_OK;
    }

    AclLiteError initCropAndPasteResource(ImageData& inputImage) {
      if (ACLLITE_OK != initCropAndPasteInputDesc(inputImage)) {
        ACLLITE_LOG_ERROR("Dvpp crop init input failed");
        return ACLLITE_ERROR;
      }

      if (ACLLITE_OK != initCropAndPasteOutputDesc()) {
        ACLLITE_LOG_ERROR("Dvpp crop init output failed");
        return ACLLITE_ERROR;
      }

      return ACLLITE_OK;
    }

    void destroyCropAndPasteResource() {
      if (cropArea_ != nullptr) {
        (void)acldvppDestroyRoiConfig(cropArea_);
        cropArea_ = nullptr;
      }

      if (pasteArea_ != nullptr) {
        (void)acldvppDestroyRoiConfig(pasteArea_);
        pasteArea_ = nullptr;
      }

      destoryResource();
    }

    AclLiteError cropAndPaste(ImageData& srcImage, ImageData& destImage, uint32_t targetX, uint32_t targetY) {
      if (ACLLITE_OK != initCropAndPasteResource(srcImage)) {
        ACLLITE_LOG_ERROR("Dvpp cropandpaste failed for init error");
        return ACLLITE_ERROR;
      }

      //设置图片裁剪参数（暂不实现图片越界处理）
      //必须为偶数
      uint32_t cropLeftOffset = 0;
      uint32_t cropTopOffset = 0;
      
      //必须为奇数
      uint32_t cropRightOffset = ((originalImageWidth_ >> 1) << 1) - 1;
      uint32_t cropBottomOffset = ((originalImageHeight_ >> 1) << 1) - 1;
      
      //设置输入图片裁剪的ROI区域
      cropArea_ = acldvppCreateRoiConfig(cropLeftOffset, cropRightOffset,
        cropTopOffset, cropBottomOffset);
      if (cropArea_ == nullptr) {
        ACLLITE_LOG_ERROR("acldvppCreateRoiConfig cropArea_ failed");
        return ACLLITE_ERROR;
      }

      //设置图片叠加参数
      uint32_t ltHorz_ = 0; //左上X坐标
      uint32_t rbHorz_ = 0; //右下X坐标
      uint32_t ltVert_ = 0; //左上Y坐标
      uint32_t rbVert_ = 0; //右下Y坐标
      uint32_t pasteWidth = rbHorz_ - ltHorz_;
      uint32_t pasteHeight = rbVert_ - ltVert_;
      
      // set crop area:
      float rx = (float)originalImageWidth_ / (float)pasteWidth;
      float ry = (float)originalImageHeight_ / (float)pasteHeight;

      int dx = 0;
      int dy = 0;
      float r = 0.0f;
      if (rx > ry) {
        dx = 0;
        r = rx;
        dy = (pasteHeight - originalImageHeight_ / r) / 2;
      }
      else {
        dy = 0;
        r = ry;
        dx = (pasteWidth - originalImageWidth_ / r) / 2;
      }

      // must even
      uint32_t pasteLeftOffset = targetX & ~1;
      // must even
      uint32_t pasteTopOffset = targetY & ~1;
      // must odd
      uint32_t pasteRightOffset = pasteWidth - 2 * dx;
      // must odd
      uint32_t pasteBottomOffset = pasteHeight - 2 * dy;

      pasteArea_ = acldvppCreateRoiConfig(pasteLeftOffset, pasteRightOffset,
        pasteTopOffset, pasteBottomOffset);
      if (pasteArea_ == nullptr) {
        ACLLITE_LOG_ERROR("acldvppCreateRoiConfig pasteArea_ failed");
        return ACLLITE_ERROR;
      }

      // crop and patse pic
      aclError aclRet = acldvppVpcCropAndPasteAsync(channelDesc, inputPicDesc,
        outputPicDesc, cropArea_, pasteArea_, stream_);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("acldvppVpcCropAndPasteAsync failed, aclRet = %d", aclRet);
        return ACLLITE_ERROR;
      }
      // END
      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("crop and paste aclrtSynchronizeStream failed, aclRet = %d", aclRet);
        return ACLLITE_ERROR;
      }
      
      destImage.width = pasteWidth - 2 * dx;
      destImage.height = pasteHeight - 2 * dy;
      destImage.alignWidth = ALIGN_UP16(pasteWidth);
      destImage.alignHeight = ALIGN_UP2(pasteHeight);
      destImage.size = outDevBufSize;
      destImage.data = SHARED_PTR_DVPP_BUF(outDevBuf);
      
      destroyCropAndPasteResource();
      
      return ACLLITE_OK;
    }

    aclrtStream stream_;
    bool isClose = false;
    void* outDevBuf; // vpc output buffer
    uint32_t outDevBufSize;  // vpc output size
    acldvppPicDesc* inputPicDesc; // vpc input desc
    acldvppPicDesc* outputPicDesc; // vpc output desc
    acldvppChannelDesc* channelDesc;

    //resize
    acldvppResizeConfig* resizeConfig_;
    Resolution size_;

    //overlay
    Resolution overlayOutSize_;
    uint32_t originalImageWidth_ = 0;
    uint32_t originalImageHeight_ = 0;
    acldvppRoiConfig* cropArea_;
    acldvppRoiConfig* pasteArea_;
  };
}