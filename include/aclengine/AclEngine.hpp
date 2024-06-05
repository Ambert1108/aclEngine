#pragma once
#include "core/Types.h"
#include "core/SafeQueue.hpp"
 
#include "acl/acl.h"
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
      bool flag = false;

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

      if (isWork.load()) {
        isWork.store(false);
        flag = true;
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
        inputPicDesc_ = nullptr;
      }

      if (vencStream_) {
        aclrtSynchronizeStream(vencStream_);
        aclError ret = aclrtUnSubscribeReport(threadId_, vencStream_);
        if (ret != ACL_SUCCESS) {
          E_LOG("[Encoder::close] unSubscribe stream failed, error={}", ret);
        }
        ret = aclrtDestroyStream(vencStream_);
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

      if (flag) {
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
      if (inputPicDesc_) {
        void* data = acldvppGetPicDescData(inputPicDesc_);
        if (!data) {
          acldvppFree(data);
        }
        acldvppDestroyPicDesc(inputPicDesc_);
        inputPicDesc_ = nullptr;
      }

      if (!inputPicDesc_) inputPicDesc_ = acldvppCreatePicDesc();
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
      if (encodeCtx.context == nullptr) {
        aclrtGetCurrentContext(&encodeCtx.context);
      }
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

  class ImageReader {
  public:
    ImageReader() {};
    ~ImageReader() {};

    bool open() {
      aclError aclRet = aclrtCreateStream(&stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageReader::open] Create venc stream failed, error={}", aclRet);
        return ACLLITE_ERROR_CREATE_STREAM;
      }

      aclrtGetRunMode(&runMode);

      channelDesc = acldvppCreateChannelDesc();
      if (channelDesc == nullptr) {
        E_LOG("[ImageReader::open] Create dvpp channel desc failed");
        return ACLLITE_ERROR_CREATE_DVPP_CHANNEL_DESC;
      }

      //auto socVersion = aclrtGetSocName();
      //if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0) {
      //  //mode: 指定通道描述信息中的通道模式，明确图片数据处理通道用于实现哪种功能，目前支持//VPC、JPEGD、JPEGE、PNGD功能
      //  aclRet = acldvppSetChannelDescMode(channelDesc, DVPP_CHNMODE_JPEGD);
      //  if (aclRet != ACL_SUCCESS) {
      //    E_LOG("[ImageReader::open] acldvppCreateChannel failed, aclRet={}", aclRet);
      //    return ACLLITE_ERRROR_CREATE_DVPP_CHANNEL;
      //  }
      //}

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

      destoryResource();

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

    AclImage imgread(const std::string& file) {
      AclImage data;
      imgreadHandle(file, data);
      return data;
    }

  protected:
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

  private:
    acldvppPixelFormat checkJpegFormat(acldvppJpegFormat format) {
      switch (format) {
      case ACL_JPEG_CSS_444:
        return PIXEL_FORMAT_YUV_SEMIPLANAR_444;
      case ACL_JPEG_CSS_422:
        return PIXEL_FORMAT_YUV_SEMIPLANAR_422;
      case ACL_JPEG_CSS_420:
        return PIXEL_FORMAT_YUV_SEMIPLANAR_420;
      case ACL_JPEG_CSS_GRAY:
        return PIXEL_FORMAT_U8C1;
      case ACL_JPEG_CSS_440:
        return PIXEL_FORMAT_YUV_SEMIPLANAR_440;
      case ACL_JPEG_CSS_411:
        return PIXEL_FORMAT_UNKNOWN;
      case ACL_JPEG_CSS_UNKNOWN:
        return PIXEL_FORMAT_UNKNOWN;
      default:
        return PIXEL_FORMAT_UNKNOWN;
      }
    }

    void* loadImageInBuffer(PicDesc& picDesc, uint32_t& picDevBufferSize) {
      if (picDesc.picName.empty()) {
        E_LOG("picture file name is empty");
        return nullptr;
      }

      FILE* fp = fopen(picDesc.picName.c_str(), "rb");
      if (fp == nullptr) {
        E_LOG("open file={} failed", picDesc.picName);
        return nullptr;
      }

      fseek(fp, 0, SEEK_END);
      uint32_t fileLen = ftell(fp);
      fseek(fp, 0, SEEK_SET);

      uint32_t inputBuffSize = fileLen;

      char* inputBuff = new(std::nothrow) char[inputBuffSize];
      size_t readSize = fread(inputBuff, sizeof(char), inputBuffSize, fp);
      if (readSize < inputBuffSize) {
        E_LOG("need read file={} {} bytes, but only {} readed",
          picDesc.picName, inputBuffSize, readSize);
        delete[] inputBuff;
        fclose(fp);
        return nullptr;
      }

      aclError aclRet = acldvppJpegGetImageInfoV2(inputBuff, inputBuffSize, &picDesc.width, &picDesc.height,
        nullptr, &picDesc.format);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("get jpeg image info failed, errorCode is {}", static_cast<int32_t>(aclRet));
        delete[] inputBuff;
        fclose(fp);
        return nullptr;
      }

      aclRet = acldvppJpegPredictDecSize(inputBuff, inputBuffSize, checkJpegFormat(picDesc.format), &picDesc.jpegDecodeSize);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("get jpeg decode size failed, errorCode is {}", static_cast<int32_t>(aclRet));
        delete[] inputBuff;
        fclose(fp);
        return nullptr;
      }

      I_LOG("get jpeg image info successed, width={}, height={}, pixel format={}, jpg format={}, jpegDecodeSize={}",
        picDesc.width, picDesc.height, checkJpegFormat(picDesc.format), picDesc.format, picDesc.jpegDecodeSize);

      void* inBufferDev = nullptr;
      aclError ret = acldvppMalloc(&inBufferDev, inputBuffSize);
      if (ret != ACL_SUCCESS) {
        delete[] inputBuff;
        E_LOG("malloc device data buffer failed, aclRet is {}", ret);
        fclose(fp);
        return nullptr;
      }

      if (runMode == ACL_HOST) {
        ret = aclrtMemcpy(inBufferDev, inputBuffSize, inputBuff, inputBuffSize, ACL_MEMCPY_HOST_TO_DEVICE);
      }
      else {
        ret = aclrtMemcpy(inBufferDev, inputBuffSize, inputBuff, inputBuffSize, ACL_MEMCPY_DEVICE_TO_DEVICE);
      }
      if (ret != ACL_SUCCESS) {
        E_LOG("memcpy failed. Input host buffer size is {}",
          inputBuffSize);
        acldvppFree(inBufferDev);
        delete[] inputBuff;
        fclose(fp);
        return nullptr;
      }

      delete[] inputBuff;
      picDevBufferSize = inputBuffSize;
      fclose(fp);
      return inBufferDev;
    }

    AclLiteError initDecodeOutputDesc(const PicDesc& pic, AclImage& inputImage) {
      auto socVersion = aclrtGetSocName();
      if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0) {
        inputImage.width = ALIGN_UP2(pic.width);
        inputImage.height = ALIGN_UP2(pic.height);
        inputImage.widthStride = ALIGN_UP64(pic.width); // 64-byte alignment
        inputImage.heightStride = ALIGN_UP16(pic.height); // 16-byte alignment
      }
      else {
        inputImage.width = inputImage.width;
        inputImage.height = inputImage.height;
        inputImage.widthStride = ALIGN_UP128(inputImage.width); // 128-byte alignment
        inputImage.heightStride = ALIGN_UP16(inputImage.height); // 16-byte alignment
      }
      if (inputImage.widthStride == 0 || inputImage.heightStride == 0) {
        E_LOG("Input image width {} or height {} invalid",
          inputImage.width, inputImage.height);
        return ACLLITE_ERROR_INVALID_ARGS;
      }

      inputImage.format = checkJpegFormat(pic.format);
      inputImage.size = pic.jpegDecodeSize;
      aclError aclRet = acldvppMalloc(&inputImage.data, inputImage.size);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("Malloc dvpp memory failed, error:{}", aclRet);
        return ACLLITE_ERROR_MALLOC_DVPP;
      }

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        E_LOG("Create dvpp pic desc failed");
        return ACLLITE_ERROR_CREATE_PIC_DESC;
      }

      acldvppSetPicDescData(outputPicDesc, inputImage.data);
      acldvppSetPicDescFormat(outputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, inputImage.widthStride);
      acldvppSetPicDescHeightStride(outputPicDesc, inputImage.heightStride);
      acldvppSetPicDescSize(outputPicDesc, inputImage.size);

      return ACLLITE_OK;
    }

    AclLiteError imgreadHandle(const std::string& file, AclImage& dest) {
      PicDesc pic = { file, 0, 0 };
      uint32_t picDevBufferSize = 0;
      void* picDevBuffer = loadImageInBuffer(pic, picDevBufferSize);
      if (picDevBuffer == nullptr) {
        E_LOG("get pic device buffer failed,index is 0");
        return ACLLITE_ERROR;
      }

      AclLiteError ret = initDecodeOutputDesc(pic, dest);
      if (ret != ACL_SUCCESS) {
        E_LOG("init jpg decode output source failed, ret={}", ret);
        return ACLLITE_ERROR;
      }

      ret = acldvppJpegDecodeAsync(channelDesc, picDevBuffer, picDevBufferSize,
        outputPicDesc, stream_);
      if (ret != ACL_SUCCESS) {
        E_LOG("acldvppJpegDecodeAsync failed, ret={}", ret);
        return ACLLITE_ERROR;
      }

      ret = aclrtSynchronizeStream(stream_);
      if (ret != ACL_SUCCESS) {
        E_LOG("aclrtSynchronizeStream failed");
        return ACLLITE_ERROR;
      }

      return ACLLITE_OK;
    }

    aclrtRunMode runMode;
    aclrtStream stream_;
    bool isClose = false;
    void* outDevBuf; // vpc output buffer
    uint32_t outDevBufSize;  // vpc output size
    acldvppPicDesc* inputPicDesc; // vpc input desc
    acldvppPicDesc* outputPicDesc; // vpc output desc
    acldvppChannelDesc* channelDesc;
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

      aclrtGetRunMode(&runMode);

      channelDesc = acldvppCreateChannelDesc();
      if (channelDesc == nullptr) {
        E_LOG("[ImageHandler::open] Create dvpp channel desc failed");
        return ACLLITE_ERROR_CREATE_DVPP_CHANNEL_DESC;
      }

      //auto socVersion = aclrtGetSocName();
      //if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0 && mode != "") {
      //  //mode: 指定通道描述信息中的通道模式，明确图片数据处理通道用于实现哪种功能，目前支持VPC、JPEGD、JPEGE、PNGD功能
      //  aclRet = acldvppSetChannelDescMode(channelDesc, STR2MODE[mode]);
      //  if (aclRet != ACL_SUCCESS) {
      //    E_LOG("[ImageHandler::open] acldvppCreateChannel failed, aclRet={}", aclRet);
      //    return ACLLITE_ERRROR_CREATE_DVPP_CHANNEL;
      //  }
      //}

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

    AclLiteError resize(const ImageData& src, ImageData& dest, uint32_t width, uint32_t height) {
      if (src.width == width || src.height == height) {
        E_LOG("[ImageHandler::resize] src width={}, height={} equal to target size", src.width, src.height);
        return ACLLITE_ERROR_DEST_INVALID;
      }
      size_.width = width;
      size_.height = height;
      AclLiteError ret = resizeExecute(src, dest);
      return ret;
    }

    AclLiteError crop(ImageData& src1, ImageData& dest, uint32_t targetX, uint32_t targetY, 
      uint32_t width , uint32_t height) {
      AclLiteError ret = cropHandle(src1, dest, targetX, targetY, width, height);
      return ret;
    }

    AclLiteError overlay(AclImage& src1, ImageData& dest, uint32_t targetX, uint32_t targetY) {
      AclLiteError ret = pasteHandle(src1, dest, targetX, targetY);
      return ret;
    }

  private:
    //vpc通用资源释放接口
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
    AclLiteError initResizeInputDesc(const ImageData& inputImage) {
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
        //inputImage.width = ALIGN_UP2(inputImage.width);
        //inputImage.height = ALIGN_UP2(inputImage.height);
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
      acldvppSetPicDescWidth(inputPicDesc, ALIGN_UP2(inputImage.width));
      acldvppSetPicDescHeight(inputPicDesc, ALIGN_UP2(inputImage.height));
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

    AclLiteError initResizeResource(const ImageData& inputImage) {
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

    AclLiteError resizeExecute(const ImageData& srcImage, ImageData& resizedImage) {
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
        resizedImage.alignWidth = ALIGN_UP16(size_.width);
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

    //裁剪/叠加功能实现

    //裁剪/叠加功能输入图片描述初始化
    AclLiteError initCropOrPasteInputDesc(AclImage& inputImage) {
      uint32_t alignWidth = ALIGN_UP16(inputImage.width);
      uint32_t alignHeight = ALIGN_UP2(inputImage.height);
      if (alignWidth == 0 || alignHeight == 0) {
        E_LOG("Invalid image parameters, width={}, height={}",
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
        E_LOG("Dvpp only support yuv and rgb format.");
        return ACLLITE_ERROR;
      }

      if (inputImage.data == nullptr) {
        E_LOG("input image data is nullptr");
        acldvppFree(inputBuffer);
        inputBuffer = nullptr;
        return ACLLITE_ERROR;
      }

      if (inputImage.size == 0) {
        E_LOG("input image size {}", inputImage.size);
        acldvppFree(inputBuffer);
        inputBuffer = nullptr;
        return ACLLITE_ERROR;
      }

      //aclError ret = acldvppMalloc(&inputBuffer, inputBufferSize);
      //ret = aclrtMemcpy(inputBuffer, inputBufferSize, inputImage.data, inputBufferSize,
      //  ACL_MEMCPY_DEVICE_TO_DEVICE); 
      //if (ret != ACL_SUCCESS) {
      //  E_LOG("memcpy failed. Input buffer size is {}, ret={}",
      //    inputBufferSize, ret);
      //  acldvppFree(inputBuffer);
      //  inputBuffer = nullptr;
      //  return ACLLITE_ERROR;
      //}

      inputPicDesc = acldvppCreatePicDesc();
      if (inputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
      }
      D_LOG("paste w:{}/h:{} wstride:{}/hstride:{} format:{}, size:{}",
        inputImage.width, inputImage.height, alignWidth, alignHeight, inputImage.format, inputBufferSize);

      acldvppSetPicDescData(inputPicDesc, inputImage.data);
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(inputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputBufferSize);

      return ACLLITE_OK;
    }

    //裁剪输出图片描述初始化
    AclLiteError initCropOutputDesc(ImageData& inputImage) {
      int outWidth = cropOutSize_.width;
      int outHeight = cropOutSize_.height;
      int outWidthStride = ALIGN_UP16(outWidth);
      int outHeightStride = ALIGN_UP2(outHeight);
      if (outWidthStride == 0 || outHeightStride == 0) {
        ACLLITE_LOG_ERROR("Crop image align widht(%d) and height(%d) failed",
          cropOutSize_.width, cropOutSize_.height);
        return ACLLITE_ERROR;
      }

      outDevBufSize = YUV420SP_SIZE(outWidthStride,
        outHeightStride);
      aclError aclRet = acldvppMalloc(&outDevBuf, outDevBufSize);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("Dvpp crop malloc output memory failed, crop "
          "width %d, height %d size %d, error %d",
          cropOutSize_.width, cropOutSize_.height,
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
      acldvppSetPicDescWidth(outputPicDesc, outWidth);
      acldvppSetPicDescHeight(outputPicDesc, outHeight);
      acldvppSetPicDescWidthStride(outputPicDesc, outWidthStride);
      acldvppSetPicDescHeightStride(outputPicDesc, outHeightStride);
      acldvppSetPicDescSize(outputPicDesc, outDevBufSize);

      return ACLLITE_OK;
    }

    //叠加输出图片描述初始化
    AclLiteError initPasteOutputDesc(ImageData& inputImage) {
      uint32_t widthStride = ALIGN_UP16(inputImage.width);
      uint32_t heightStride = ALIGN_UP2(inputImage.height);
      if (!inputImage.data) {
        E_LOG("[ImageHandler::initPasteOutputDesc] bottom img can not be nullptr");
        return ACLLITE_ERROR;
      }
      
      if (inputImage.size == 0) {
        E_LOG("[ImageHandler::initPasteOutputDesc] bottom img size can not be zero");
        return ACLLITE_ERROR;
      }

      if (inputImage.width == 0 || inputImage.height == 0) {
        E_LOG("[ImageHandler::initPasteOutputDesc] bottom img width({}) and height({}) is invaild",
          inputImage.width, inputImage.height);
        return ACLLITE_ERROR;
      }

      uint32_t outputBufferSize = YUV420SP_SIZE(widthStride, heightStride);
      //auto aclRet = acldvppMalloc(&outputBuffer, outputBufferSize);
      //aclRet = aclrtMemcpy(outputBuffer, outputBufferSize, inputImage.data.get(), inputImage.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
      //if (aclRet != ACL_SUCCESS) {
      //  E_LOG("acl memcpy data to dev failed, image.size={}, ret={}", outputBufferSize, aclRet);
      //  (void)acldvppFree(outputBuffer);
      //  outputBuffer = nullptr;
      //  return false;
      //}
      D_LOG("paste w:{}/h:{} wstride:{}/hstride:{} format:{}, size:{}",
        inputImage.width, inputImage.height, widthStride, heightStride, inputImage.format, outputBufferSize);

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        ACLLITE_LOG_ERROR("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
      }
      acldvppSetPicDescData(outputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(outputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, widthStride);
      acldvppSetPicDescHeightStride(outputPicDesc, heightStride);
      acldvppSetPicDescSize(outputPicDesc, outputBufferSize);

      return ACLLITE_OK;
    }

    void destroyCropOrPasteResource() {
      if (cropArea_ != nullptr) {
        (void)acldvppDestroyRoiConfig(cropArea_);
        cropArea_ = nullptr;
      }

      if (pasteArea_ != nullptr) {
        (void)acldvppDestroyRoiConfig(pasteArea_);
        pasteArea_ = nullptr;
      }
      if (inputBuffer != nullptr) {
        (void)acldvppFree(inputBuffer);
        inputBuffer = nullptr;
      }
      if (outputBuffer != nullptr) {
        (void)acldvppFree(outputBuffer);
        outputBuffer = nullptr;
      }

      destoryResource();
    }

    AclLiteError cropHandle(ImageData& topImg, ImageData& destImage,
      uint32_t targetX, uint32_t targetY, uint32_t width, uint32_t height) {

      return ACLLITE_OK;
    }

    AclLiteError pasteHandle(AclImage& topImg, ImageData& destImg, uint32_t targetX, uint32_t targetY) {
      //初始化输入图片信息描述
      if (initCropOrPasteInputDesc(topImg) != ACLLITE_OK) {
        return ACLLITE_ERROR;
      }

      //若右偏移或下偏移大于被贴对象的宽度或高度，则需要裁剪
      //if(pasteRightOffset > ) 
      //计算裁剪ROI区域
      //必须为偶数
      uint32_t cropLeftOffset = 0; //相对输入图片的左偏移
      uint32_t cropTopOffset = 0; //相对输入图片的上偏移
      
      //必须为奇数
      uint32_t cropRightOffset = topImg.width - ((topImg.width & 1) ^ 1);  //相对输入图片的右偏移
      uint32_t cropBottomOffset = topImg.height - ((topImg.height & 1) ^ 1); //相对输入图片的下偏移
      //uint32_t cropRightOffset = 299;  //相对输入图片的右偏移
      //uint32_t cropBottomOffset = 399; //相对输入图片的下偏移
      
      //设置输入图片裁剪的ROI区域
      cropArea_ = acldvppCreateRoiConfig(cropLeftOffset, cropRightOffset,
        cropTopOffset, cropBottomOffset);
      if (cropArea_ == nullptr) {
        E_LOG("acldvppCreateRoiConfig cropArea_ failed");
        return ACLLITE_ERROR;
      }

      //初始化输出图片信息描述
      if (initPasteOutputDesc(destImg) != ACLLITE_OK) {
        return ACLLITE_ERROR;
      }

      //计算叠加ROI区域
      // 必须为偶数
      // 左偏移必须满足16对齐
      uint32_t pasteLeftOffset = (targetX / 16) * 16;
      uint32_t pasteTopOffset = targetY - (targetY & 1);
      //uint32_t pasteLeftOffset = 16;
      //uint32_t pasteTopOffset = 200;

      // 必须为奇数(Ascend 310P无要求)
      uint32_t pasteRightOffset = pasteLeftOffset + topImg.width;
      pasteRightOffset = pasteRightOffset - ((pasteRightOffset & 1) ^ 1);
      uint32_t pasteBottomOffset = pasteTopOffset + topImg.height;
      pasteBottomOffset = pasteBottomOffset - ((pasteBottomOffset & 1) ^ 1);
      //uint32_t pasteRightOffset = pasteLeftOffset + 300 - 1;  // must odd
      //uint32_t pasteBottomOffset = pasteTopOffset + 400 - 1;  // must odd
      
      pasteArea_ = acldvppCreateRoiConfig(pasteLeftOffset, pasteRightOffset,
        pasteTopOffset, pasteBottomOffset);
      if (pasteArea_ == nullptr) {
        E_LOG("acldvppCreateRoiConfig pasteArea_ failed");
        return ACLLITE_ERROR;
      }

      aclError aclRet = acldvppVpcCropAndPasteAsync(channelDesc, inputPicDesc,
        outputPicDesc, cropArea_, pasteArea_, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("acldvppVpcCropAndPasteAsync failed, aclRet={}", aclRet);
        return ACLLITE_ERROR;
      }

      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("crop and paste aclrtSynchronizeStream failed, aclRet={}", aclRet);
        return ACLLITE_ERROR;
      }

      destroyCropOrPasteResource();
      
      return ACLLITE_OK;
    }

    aclrtRunMode runMode;
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

    //crop and paste
    Resolution cropOutSize_;
    void* inputBuffer = nullptr;
    void* outputBuffer = nullptr;
    acldvppRoiConfig* cropArea_;
    acldvppRoiConfig* pasteArea_;
  };
}