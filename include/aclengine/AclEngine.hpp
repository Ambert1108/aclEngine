// @brief: 昇腾能力引擎
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.5.20]
// @version: V0.0.3
// @revision: [Ambert@2024.7.1]

#pragma once
#include "core/Types.h"
#include "core/Utils.hpp"
#include "core/SafeQueue.hpp"
#include "core/Error.h"
 
#include "acl/acl.h"
//#include "refer/AcleError.h"
//#include "refer/AclLiteResource.h"

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
  class Resource {
  public:
    Resource() :isReleased_(false), useDefaultCtx_(true), deviceId_(0),
      runMode_(ACL_HOST), context_(nullptr), aclConfig_("") {};

    Resource(int32_t devId, const std::string& aclConfigPath,
      bool useDefaultCtx = true) :isReleased_(false),
      useDefaultCtx_(useDefaultCtx), deviceId_(devId),
      runMode_(ACL_HOST), context_(nullptr), aclConfig_(aclConfigPath) {};

    ~Resource() { Release(); }

    AcleError Init() {
      aclError ret = aclrtSetDevice(deviceId_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Acl open device {} failed, errorCode is {}", deviceId_, ret);
        return ACLE_ERROR;
      }
      I_LOG("Open device {} ok", deviceId_);

      if (useDefaultCtx_) {
        I_LOG("Use default context currently");
        ret = aclrtGetCurrentContext(&context_);
        if ((ret != ACL_SUCCESS) || (context_ == nullptr)) {
          E_LOG("Get current acl context failed, errorCode is {}", ret);
          return ACLE_ERROR_GET_ACL_CONTEXT;
        }
      }
      else {
        ret = aclrtCreateContext(&context_, deviceId_);
        if (ret != ACL_SUCCESS) {
          E_LOG("Create acl context failed, errorCode is {}", ret);
          return ACLE_ERROR_CREATE_ACL_CONTEXT;
        }
      }

      ret = aclrtGetRunMode(&runMode_);
      if (ret != ACL_SUCCESS) {
        W_LOG("acl get run mode failed, errorCode is {}", ret);
      }
      I_LOG("runMode={}", runMode_);

      return ACLE_OK;
    }
    void Release() {
      if (isReleased_) return;

      aclError ret;
      if ((!useDefaultCtx_) && (context_ != nullptr)) {
        ret = aclrtDestroyContext(context_);
        if (ret != ACL_SUCCESS) {
          E_LOG("destroy context failed, errorCode is  {}", ret);
        }
        context_ = nullptr;
      }
      I_LOG("destroy context ok");

      ret = aclrtResetDevice(deviceId_);
      if (ret != ACL_SUCCESS) {
        E_LOG("reset device failed, errorCode is {}", ret);
      }
      I_LOG("Reset device {} ok", deviceId_);

      isReleased_ = true;
    }
    aclrtRunMode GetRunMode() { return runMode_; }
    aclrtContext GetContext() { return context_; }
    aclrtContext GetContextByDevice(int32_t devId);

  private:
    bool isReleased_;
    bool useDefaultCtx_;
    int32_t deviceId_;
    aclrtRunMode runMode_;
    aclrtContext context_;
    std::string aclConfig_;
  };

#define INVALID_CHANNEL_ID (-1)
#define VIDEO_CHANNEL_MAX  (256)
  class ChannelIdGenerator {
  public:
    ChannelIdGenerator()
    {
      for (int i = 0; i < VIDEO_CHANNEL_MAX; i++) {
        channelId_[i] = INVALID_CHANNEL_ID;
      }
    }
    ~ChannelIdGenerator() {};

    int GenerateChannelId(void)
    {
      std::lock_guard<std::mutex> lock(mutex_lock_);
      for (int i = 0; i < VIDEO_CHANNEL_MAX; i++) {
        if (channelId_[i] == INVALID_CHANNEL_ID) {
          channelId_[i] = i;
          return i;
        }
      }

      return INVALID_CHANNEL_ID;
    }

    void ReleaseChannelId(int channelId)
    {
      std::lock_guard<std::mutex> lock(mutex_lock_);
      if ((channelId >= 0) && (channelId < VIDEO_CHANNEL_MAX)) {
        channelId_[channelId] = INVALID_CHANNEL_ID;
      }
    }

  private:
    int channelId_[VIDEO_CHANNEL_MAX];
    mutable std::mutex mutex_lock_;
  };
  inline ChannelIdGenerator channelIdGenerator{};

  class Decoder {
  public:
    Decoder(CodecFormat& fmt) : decodeCtx(fmt) {};

    ~Decoder() { close(); };

    bool open() {
      alignWidth_ = ALIGN_UP16(decodeCtx.width);
      alignHeight_ = ALIGN_UP2(decodeCtx.height);
      outputPicSize_ = YUV420SP_SIZE(alignWidth_, alignHeight_);

      channelId_ = channelIdGenerator.GenerateChannelId();
      if (channelId_ == INVALID_CHANNEL_ID || channelId_ >= VIDEO_CHANNEL_MAX) {
        E_LOG("[Decoder::open] Decoder number excessive {}", VIDEO_CHANNEL_MAX);
        return false;
      }

      return initSource();
    };

    void close() {
      if (!needClose) return;
      sendEosToVdec();

      if (isWork.load()) isWork.store(false);
      aclError ret;
      if (inputStreamDesc_ != nullptr) {
        void* inputBuf = acldvppGetStreamDescData(inputStreamDesc_);
        if (inputBuf != nullptr) {
          acldvppFree(inputBuf);
        }
        aclError ret = acldvppDestroyStreamDesc(inputStreamDesc_);
        if (ret != ACL_SUCCESS) {
          E_LOG("fail to destroy input stream desc");
        }
        inputStreamDesc_ = nullptr;
      }

      if (outputPicDesc_ != nullptr) {
        void* outputBuf = acldvppGetPicDescData(outputPicDesc_);
        if (outputBuf != nullptr) {
          acldvppFree(outputBuf);
        }
        aclError ret = acldvppDestroyPicDesc(outputPicDesc_);
        if (ret != ACL_SUCCESS) {
          E_LOG("fail to destroy output pic desc");
        }
        outputPicDesc_ = nullptr;
      }

      if (vdecChannelDesc_ != nullptr) {
        ret = aclvdecDestroyChannel(vdecChannelDesc_);
        if (ret != ACL_SUCCESS) {
          E_LOG("Vdec destroy channel failed, error:{}", ret);
        }
        aclvdecDestroyChannelDesc(vdecChannelDesc_);
        vdecChannelDesc_ = nullptr;
      }

      unsubscribReportThread();

      while (!frameQueue.Empty()) {
        AclFrame frame = frameQueue.Pop();
        if (frame.data != nullptr) {
          acldvppFree(frame.data.get());
          frame.data = nullptr;
        }
      }

      channelIdGenerator.ReleaseChannelId(channelId_);
      needClose = false;
      I_LOG("[AclEngine::Decoder] Decoder is closed");
    }

    int readFrame(std::shared_ptr<AclPacket> packet, AclFrame& frame) {
      aclError ret = createInputStreamDesc(packet);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Decoder::readFrame] create input stream description failed, error:{}", ret);
        return -1;
      }

      ret = createOutputPicDesc(outputPicSize_);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Decoder::readFrame] create input stream description failed, error:{}", ret);
        return -1;
      }

      ret = aclvdecSendFrame(vdecChannelDesc_, inputStreamDesc_,
        outputPicDesc_, nullptr, (void*)this);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Decoder::readFrame] Send frame to vdec failed, errorno:{}", ret);
        return -1;
      }

      if (frameQueue.Empty()) {
        W_LOG("[Decoder::readFrame] get frame failed, wait decode process");
        return 1;
      }
      frame = frameQueue.Pop();

      return 0;
    }

  private:
    static void callback(acldvppStreamDesc* input, acldvppPicDesc* output, void* userData) {
      Decoder* own = (Decoder*)userData;
      if (!own->isWork.load()) return;
      uint32_t retCode = acldvppGetStreamDescRetCode(input);
      if (retCode != 0) {
        E_LOG("[Decoder::callback] get decode out data failed");
        return;
      }
      // Get decoded image parameters
      AclFrame frame;
      frame.format = acldvppGetPicDescFormat(output);
      frame.width = acldvppGetPicDescWidth(output);
      frame.height = acldvppGetPicDescHeight(output);
      frame.alignWidth = acldvppGetPicDescWidthStride(output);
      frame.alignHeight = acldvppGetPicDescHeightStride(output);
      frame.size = acldvppGetPicDescSize(output);

      void* vdecOutBufferDev = acldvppGetPicDescData(output);
      frame.data = SHARED_PTR_DVPP_BUF(vdecOutBufferDev);
      own->frameQueue.Push(frame);

      // Release resouce
      if (output) {
        aclError ret = acldvppDestroyPicDesc(output);
        if (ret != ACL_SUCCESS) {
          E_LOG("fail to destroy pic desc, error {}", ret);
        }
        output = nullptr;
      }

      if (input) {
        //void* inputBuf = acldvppGetStreamDescData(input);
        //if (inputBuf != nullptr) {
        //  acldvppFree(inputBuf);
        //}
        aclError ret = acldvppDestroyStreamDesc(input);
        if (ret != ACL_SUCCESS) {
          E_LOG("fail to destroy input stream desc");
        }
        input = nullptr;
      }
    }

    static void* notifyCallbackFunc(void* arg) {
      Decoder* own = (Decoder*)arg;
      if (!own->decodeCtx.context) {
        E_LOG("[Decoder::notifyCallbackFunc] notify use context can not be nullptr!");
        return nullptr;
      }
      aclError ret = aclrtSetCurrentContext(own->decodeCtx.context);
      if (ret != ACL_SUCCESS) {
        E_LOG("Video decoder set context failed, error:{}", ret);
      }

      own->isWork.store(true);
      while (own->isWork.load()) {
        aclrtProcessReport(500);
      }

      D_LOG("Vdec subscribe thread exit!");

      return (void*)ACLE_OK;
    }

    AcleError createVdecChannel() {
      vdecChannelDesc_ = aclvdecCreateChannelDesc();
      if (vdecChannelDesc_ == nullptr) {
        E_LOG("Create vdec channel desc failed");
        return ACLE_ERROR_CREATE_DVPP_CHANNEL_DESC;
      }

      // 设置通道id
      aclError ret = aclvdecSetChannelDescChannelId(vdecChannelDesc_,
        channelId_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel id to {} failed, error:{}",
          channelId_, ret);
        return ACLE_ERROR_SET_VDEC_CHANNEL_ID;
      }

      ret = aclvdecSetChannelDescThreadId(vdecChannelDesc_, threadId_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel thread id failed, error:{}", ret);
        return ACLE_ERROR_SET_VDEC_CHANNEL_THREAD_ID;
      }

      // callback func
      ret = aclvdecSetChannelDescCallback(vdecChannelDesc_, callback);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel callback failed, error:{}", ret);
        return ACLE_ERROR_SET_VDEC_CALLBACK;
      }

      ret = aclvdecSetChannelDescEnType(vdecChannelDesc_, H264_BASELINE_LEVEL);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel entype failed, error:{}", ret);
        return ACLE_ERROR_SET_VDEC_ENTYPE;
      }

      ret = aclvdecSetChannelDescOutPicFormat(vdecChannelDesc_, decodeCtx.format);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel pic format failed, error:{}", ret);
        return ACLE_ERROR_SET_VDEC_PIC_FORMAT;
      }

      // create vdec channel
      ret = aclvdecCreateChannel(vdecChannelDesc_);
      if (ret != ACL_SUCCESS) {
        E_LOG("fail to create vdec channel, error:{}", ret);
        return ACLE_ERROR_CREATE_VDEC_CHANNEL;
      }

      return ACLE_OK;
    }

    bool initSource() {
      if (decodeCtx.context == nullptr) {
        aclError aclRet = aclrtGetCurrentContext(&decodeCtx.context);
        if ((aclRet != ACL_SUCCESS) || (decodeCtx.context == nullptr)) {
          E_LOG("[Deocder::initSource] Get current acl context error:{}", aclRet);
          return false;
        }
      }
      // Get current run mode
      aclError aclRet = aclrtGetRunMode(&decodeCtx.runMode);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[Deocder::initSource] acl get run mode failed");
        return false;
      }

      int ret = pthread_create(&threadId_, nullptr, notifyCallbackFunc, (void*)this);
      if (ret) {
        E_LOG("[Decoder::initResource] Create notify callback thread failed, errorCode={}", ret);
        return false;
      }

      //aclRet = aclrtCreateStream(&stream_);
      //if (aclRet != ACL_SUCCESS) {
      //  E_LOG("[Decoder::initResource] Vdec create stream failed, errorno:{}", aclRet);
      //  return false;
      //}
      //(void)aclrtSubscribeReport(static_cast<uint64_t>(threadId_), stream_);

      ret = createVdecChannel();
      if (ret != ACLE_OK) {
        E_LOG("Create vdec channel failed");
        return false;
      }

      return true;
    }

    AcleError createInputStreamDesc(std::shared_ptr<AclPacket> input, bool finish = false) {
      inputStreamDesc_ = acldvppCreateStreamDesc();
      if (inputStreamDesc_ == nullptr) {
        E_LOG("Create input stream desc failed");
        return ACLE_ERROR_CREATE_STREAM_DESC;
      }

      aclError ret;
      // to the last data,send an endding signal to dvpp vdec
      if (finish) {
        ret = acldvppSetStreamDescEos(inputStreamDesc_, 1);
        if (ret != ACL_SUCCESS) {
          E_LOG("Set EOS to input stream desc failed, error:{}", ret);
          return ACLE_ERROR_SET_STREAM_DESC_EOS;
        }
        return ACLE_OK;
      }

      if (!input->data) {
        E_LOG("[Decoder::createInputStreamDesc] input data is nullptr");
        return ACLE_ERROR_VDEC_INVALID_PARAM;
      }

      ret = acldvppSetStreamDescData(inputStreamDesc_, input->data);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set input stream data failed, errorno:{}", ret);
        return ACLE_ERROR_SET_STREAM_DESC_DATA;
      }

      // set size for dvpp stream desc
      ret = acldvppSetStreamDescSize(inputStreamDesc_, input->size);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set input stream size failed, errorno:{}", ret);
        return ACLE_ERROR_SET_STREAM_DESC_SIZE;
      }

      acldvppSetStreamDescTimestamp(inputStreamDesc_, input->pts);

      return ACLE_OK;
    }

    AcleError createOutputPicDesc(size_t size) {
      aclError ret = acldvppMalloc(&outputPicBuf_, size);
      if (ret != ACL_SUCCESS) {
        E_LOG("Malloc vdec output buffer failed when create "
          "vdec output desc, errorno:{}", ret);
        return ACLE_ERROR_MALLOC_DVPP;
      }

      outputPicDesc_ = acldvppCreatePicDesc();
      if (outputPicDesc_ == nullptr) {
        E_LOG("Create vdec output pic desc failed");
        return ACLE_ERROR_CREATE_PIC_DESC;
      }

      ret = acldvppSetPicDescData(outputPicDesc_, outputPicBuf_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic desc data failed, error:{}", ret);
        return ACLE_ERROR_SET_PIC_DESC_DATA;
      }

      ret = acldvppSetPicDescSize(outputPicDesc_, size);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic size failed, error:{}", ret);
        return ACLE_ERROR_SET_PIC_DESC_SIZE;
      }

      ret = acldvppSetPicDescWidth(outputPicDesc_, decodeCtx.width);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic width failed, error:{}", ret);
        return ACLE_ERROR_VDEC_SET_WIDTH;
      }

      ret = acldvppSetPicDescHeight(outputPicDesc_, decodeCtx.height);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic height failed, error:{}", ret);
        return ACLE_ERROR_VDEC_SET_HEIGHT;
      }

      ret = acldvppSetPicDescWidthStride(outputPicDesc_, alignWidth_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic widthStride failed, error:{}", ret);
        return ACLE_ERROR;
      }

      ret = acldvppSetPicDescHeightStride(outputPicDesc_, alignHeight_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic heightStride failed, error:{}", ret);
        return ACLE_ERROR;
      }

      ret = acldvppSetPicDescFormat(outputPicDesc_, decodeCtx.format);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec output pic format failed, error:{}", ret);
        return ACLE_ERROR_SET_PIC_DESC_FORMAT;
      }

      return ACLE_OK;
      return ACLE_OK;
    }

    void unsubscribReportThread() {
      if ((threadId_ == 0) || (stream_ == nullptr)) return;
      //(void)aclrtUnSubscribeReport(static_cast<uint64_t>(threadId_), stream_);
      //if (stream_ != nullptr) {
      //  aclError ret = aclrtDestroyStream(stream_);
      //  if (ret != ACL_SUCCESS) {
      //    E_LOG("Vdec destroy stream failed");
      //  }
      //  stream_ = nullptr;
      //}

      void* res = nullptr;
      int joinThreadErr = pthread_join(threadId_, &res);
      if (joinThreadErr) {
        E_LOG("Join thread failed, threadId={}, err={}",
          threadId_, joinThreadErr);
      }
      else {
        if ((uint64_t)res != 0) {
          E_LOG("thread run failed. ret is {}.", (uint64_t)res);
        }
      }
      I_LOG("Destory report thread success.");
    }

    void sendEosToVdec() {
      std::shared_ptr<AclPacket> empty = std::make_shared<AclPacket>();
      createInputStreamDesc(empty, true);
      outputPicDesc_ = acldvppCreatePicDesc();
      if (!outputPicDesc_) {
        E_LOG("Create vdec output pic desc failed");
        return;
      }
      // send data to dvpp vdec to decode
      aclError ret = aclvdecSendFrame(vdecChannelDesc_, inputStreamDesc_,
        outputPicDesc_, nullptr, (void*)this);
      if (ret != ACL_SUCCESS) {
        E_LOG("Send eos frame to vdec failed, error:{}", ret);
        return;
      }
    }

    CodecFormat decodeCtx;
    pthread_t threadId_;
    aclvdecChannelDesc* vdecChannelDesc_ = nullptr;
    acldvppStreamDesc* inputStreamDesc_ = nullptr;
    acldvppPicDesc* outputPicDesc_ = nullptr;
    void* outputPicBuf_ = nullptr;
    int outputPicSize_ = 0;
    int alignWidth_ = 0;
    int alignHeight_ = 0;
    aclrtStream stream_ = nullptr;
    SafeQueue<AclFrame> frameQueue{};
    int channelId_ = 0;
    std::atomic<bool> isWork{ false };
    bool needClose = true;
  };

  class Encoder {
  public:
    Encoder(CodecFormat& fmt) : encodeCtx(fmt) { };

    ~Encoder() { close(); }

    bool open() { 
      if (!encodeCtx.file.empty()) {
        outFp_ = fopen(encodeCtx.file.c_str(), "wb+");
        if (outFp_ == nullptr) {
          E_LOG("[Encoder::open] Open file {} failed, error={}", encodeCtx.file, strerror(errno));
          return ACLE_ERROR_OPEN_FILE;
        }
      }
      
      return initResource(); 
    }

    void close() {
      if (!needClose) return;
      needClose = false;
      bool flag = false;

      if (vencFrameConfig_) {
        AcleError ret = setFrameConfig(1, 0);
        if (ret != ACLE_OK) {
          W_LOG("[Encoder::close] Set frame config failed, error={}", ret);
        }
      }

      if (vencChannelDesc_) {
        AcleError ret = aclvencSendFrame(vencChannelDesc_, nullptr,
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

    int writeFrame(const AclFrame& input, AclPacket& packet, bool keyFrame = false) {
      AcleError ret = createInputPicDesc(input);
      if (ret != ACLE_OK) {
        E_LOG("[Encoder::writeFrame] fail to create picture description");
        return -1;
      }

      acldvppStreamDesc* outputStreamDesc = nullptr;

      ret = aclvencSendFrame(vencChannelDesc_, inputPicDesc_,
        static_cast<void*>(outputStreamDesc), vencFrameConfig_, (void*)this);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::writeFrame] encode frame failed, errorCode={}", ret);
        return -1;
      }

      if (keyFrame) setFrameConfig(0, 1);
      else setFrameConfig(0, 0);

      if (pakcetQueue.Empty()) {
        W_LOG("[Encoder::writeFrame] get packet failed, wait encode process");
        return 1;
      }
      packet = pakcetQueue.Pop();
      return 0;
    }

  private:
    AcleError saveVencFile(void* vencData, uint32_t size) {
      AcleError atlRet = ACLE_OK;
      void* data = vencData;
      if (encodeCtx.runMode == ACL_HOST) {
        data = copyDataToHost(vencData, size, encodeCtx.runMode, NORMAL);
      }
      size_t ret = fwrite(data, 1, size, outFp_);
      if (ret != size) {
        E_LOG("[Encoder::saveVencFile] Save venc file {} failed, need write {} bytes, "
          "but only write {} bytes, error: {}",
          encodeCtx.file, size, ret, strerror(errno));
        atlRet = ACLE_ERROR_WRITE_FILE;
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
        data = copyDataToHost(data, size, ACL_HOST, NORMAL);
        AclPacket pkt;
        pkt.data = data;
        pkt.size = acldvppGetStreamDescSize(output);
        pkt.pts = acldvppGetStreamDescTimestamp(output);
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

    AcleError createVencChannel() {
      vencChannelDesc_ = aclvencCreateChannelDesc();
      if (vencChannelDesc_ == nullptr) {
        E_LOG("[Encoder::createVencChannel] Create venc channel desc failed");
        return ACLE_ERROR_CREATE_VENC_CHAN_DESC;
      }
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_THREAD_ID_UINT64, 8, &threadId_);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_CALLBACK_PTR, 8, &callback);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_ENCODE_TYPE_UINT32, 4, &encodeCtx.enType);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_PIXEL_FORMAT_UINT32, 4, &encodeCtx.format);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_PIC_WIDTH_UINT32, 4, &encodeCtx.width);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_PIC_HEIGHT_UINT32, 4, &encodeCtx.height);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_KEY_FRAME_INTERVAL_UINT32, 4, &encodeCtx.gopSize);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_RC_MODE_UINT32, 4, &encodeCtx.rcMode);
      //aclvencSetChannelDescParam(vencChannelDesc_, ACLE_VENC_MAX_BITRATE_UINT32, 4, &encodeCtx.maxBitrate);
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
        return ACLE_ERROR_CREATE_VENC_CHAN;
      }

      return ACLE_OK;
    }

    AcleError createFrameConfig() {
      vencFrameConfig_ = aclvencCreateFrameConfig();
      if (vencFrameConfig_ == nullptr) {
        E_LOG("[Encoder::createFrameConfig] Create frame config failed");
        return ACLE_ERROR_VENC_CREATE_FRAME_CONFIG;
      }

      AcleError ret = setFrameConfig(0, 1);
      if (ret != ACLE_OK) {
        E_LOG("[Encoder::createFrameConfig] Set frame config failed, error={}", ret);
        return ret;
      }

      return ACLE_OK;
    }

    AcleError createInputPicDesc(const AclFrame& frame) {
      if (inputPicDesc_) {
        void* data = acldvppGetPicDescData(inputPicDesc_);
        if (data) {
          acldvppFree(data);
        }
        acldvppDestroyPicDesc(inputPicDesc_);
        inputPicDesc_ = nullptr;
      }

      if (!inputPicDesc_) inputPicDesc_ = acldvppCreatePicDesc();
      if (inputPicDesc_ == nullptr) {
        E_LOG("[Encoder::createInputPicDesc] Create input pic desc failed");
        return ACLE_ERROR_CREATE_PIC_DESC;
      }
      void* inBufferDev_ = nullptr;
      uint32_t inBufferSize_ = frame.size;
      auto aclRet = acldvppMalloc(&inBufferDev_, inBufferSize_);
      if (encodeCtx.runMode != ACL_DEVICE) {
        aclRet = aclrtMemcpy(inBufferDev_, inBufferSize_, frame.data.get(), frame.size, ACL_MEMCPY_HOST_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[Encoder::createInputPicDesc] acl memcpy data to dev failed, image.size={}, ret={}", frame.size, aclRet);
          (void)acldvppFree(inBufferDev_);
          inBufferDev_ = nullptr;
          return false;
        }
      }
      else {
        aclRet = aclrtMemcpy(inBufferDev_, inBufferSize_, frame.data.get(), frame.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[Encoder::createInputPicDesc] acl memcpy data to dev failed, image.size={}, ret={}", frame.size, aclRet);
          (void)acldvppFree(inBufferDev_);
          inBufferDev_ = nullptr;
          return false;
        }
      }
      acldvppSetPicDescFormat(inputPicDesc_, encodeCtx.format);
      acldvppSetPicDescWidth(inputPicDesc_, frame.width);
      acldvppSetPicDescHeight(inputPicDesc_, frame.height);
      acldvppSetPicDescWidthStride(inputPicDesc_, ALIGN_UP16(frame.width));
      acldvppSetPicDescHeightStride(inputPicDesc_, ALIGN_UP2(frame.height));
      acldvppSetPicDescData(inputPicDesc_, inBufferDev_);
      acldvppSetPicDescSize(inputPicDesc_, frame.size);

      return ACLE_OK;
    }

    AcleError setFrameConfig(uint8_t eos, uint8_t forceIFrame) {
      if (!vencFrameConfig_) return ACLE_ERROR_INVALID_ARGS;
      /* 设置是否为结束帧，0：不是，1：是结束帧 <Ambert May-21-2024> */
      aclError ret = aclvencSetFrameConfigEos(vencFrameConfig_, eos);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::setFrameConfig] fail to set eos, ret={}", ret);
        return ACLE_ERROR_VENC_SET_EOS;
      }

      /* 设置是否强制重新开启I帧间隔，0：不强制，1：强制 <Ambert May-21-2024> */
      ret = aclvencSetFrameConfigForceIFrame(vencFrameConfig_, forceIFrame);
      if (ret != ACL_SUCCESS) {
        E_LOG("[Encoder::setFrameConfig] fail to set venc ForceIFrame");
        return ACLE_ERROR_VENC_SET_IF_FRAME;
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

      AcleError ret = pthread_create(&threadId_, nullptr,
        notifyCallbackFunc, (void*)this);
      if (ret != ACLE_OK) {
        E_LOG("[Encoder::initResource] Create notify callback thread failed, errorCode={}", ret);
        return false;
      }

      ret = createVencChannel();
      if (ret != ACLE_OK) {
        E_LOG("[Encoder::initResource] Create venc channel failed, errorCode={}", ret);
        return false;
      }

      ret = createFrameConfig();
      if (ret != ACLE_OK) {
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

#define INBUF_SIZE 4096
#define BUF_SIZE 1024 * 1024
#define MTU 1300
  class Demuxer {
  public:
    Demuxer() {
    };

    ~Demuxer() {
      close();
    }

    bool open(std::string file = "") {
      if (!file.empty()) {
        int err;
        avformat_network_init();
        
        AVDictionary* avdic = nullptr;
        av_dict_set(&avdic, "buffer_size", "10485760", 0);
        av_dict_set(&avdic, "max_delay", "30", 0);
        av_dict_set(&avdic, "stimeout", "5000000", 0);
        av_dict_set(&avdic, "reorder_queue_size", "0", 0);
        av_dict_set(&avdic, "pkt_size", "10485760", 0);
        
        fmtCtx = avformat_alloc_context();
        if ((err = avformat_open_input(&fmtCtx, file.c_str(), nullptr, &avdic)) < 0) {
          E_LOG("ERROR: avformat_open_input error, open input file.");
          close();
          return false;
        }
        if (avdic) av_dict_free(&avdic);

        for (uint32_t i = 0; i < fmtCtx->nb_streams; i++) {
          if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { // check is media type is video
            videoStreamIndex = i;
            break;
          }
        }

        fileWidth = fmtCtx->streams[videoStreamIndex]->codecpar->width;
        fileHeight = fmtCtx->streams[videoStreamIndex]->codecpar->height;

        const AVBitStreamFilter* videoFilter = av_bsf_get_by_name("h264_mp4toannexb");
        if (!videoFilter) { // check video fileter is nullptr
          E_LOG("Unkonw bitstream filter, videoFilter is nullptr!");
          return false;
        }

        // checke alloc bsf context result
        if (av_bsf_alloc(videoFilter, &bsfCtx) < 0) {
          E_LOG("Fail to call av_bsf_alloc!");
          return false;
        }

        // check copy parameters result
        if (avcodec_parameters_copy(bsfCtx->par_in,
          fmtCtx->streams[videoStreamIndex]->codecpar) < 0) {
          E_LOG("Fail to call avcodec_parameters_copy!");
          return false;
        }

        bsfCtx->time_base_in = fmtCtx->streams[videoStreamIndex]->time_base;

        // check initialize bsf contextreult
        if (av_bsf_init(bsfCtx) < 0) {
          E_LOG("Fail to call av_bsf_init!");
          return false;
        }
      }
      else {
        codec = avcodec_find_decoder_by_name("h264");
        if (!codec) {
          E_LOG("Codec not found");
          close();
          return false;
        }
        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) {
          E_LOG("[Demuxer::open] codecCtx cannot be nullptr!");
          close();
          return false;
        }
        buff = new uint8_t[BUF_SIZE];
        parser = av_parser_init(AV_CODEC_ID_H264);
        if (!parser) {
          E_LOG("[Demuxer::open] use av_parser_init failed");
          close();
          return false;
        }
      }

      aclrtGetRunMode(&runMode);

      return true;
    }

    bool close() {
      if (parser) {
        av_parser_close(parser);
        parser = nullptr;
      }
      if (tmpPkt) {
        av_packet_free(&tmpPkt);
        tmpPkt = nullptr;
      }
      if (buff) {
        delete[] buff;
        buff = nullptr;
      }
      if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
      }
      if (bsfCtx) {
        av_bsf_free(&bsfCtx);
        bsfCtx = nullptr;
      }
      if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
      }
    }

    int demux(std::shared_ptr<AclPacket> packet) {
      int err = 0;
      if ((err = av_read_frame(fmtCtx, &inPacket)) < 0) {
        if (err == AVERROR_EOF) {//如果读到文件尾，返回-3，下次调用重新从文件头开始读
          W_LOG("[Demuxer::demux] av_read_frame finish, no more frame to read");
          return -3;
        }
        else {
          E_LOG("[Demuxer::demux] Fail to call av_read_frame, error:{}", err);
          av_packet_unref(&inPacket);
          return -2;
        }
      }
      if (inPacket.stream_index != videoStreamIndex) {
        av_packet_unref(&inPacket);
        return 1;
      }

      if (av_bsf_send_packet(bsfCtx, &inPacket)) {
        E_LOG("Fail to call av_bsf_send_packet");
      }

      // receive single frame from ffmpeg
      int i = 1;
      while (av_bsf_receive_packet(bsfCtx, &inPacket) == 0) {
        packet->data = copyDataToDevice(inPacket.data, inPacket.size, runMode, DVPP);
        packet->size = inPacket.size;
        if (i > 1) I_LOG("receive {} packet", i);
        //I_LOG("demux frame:{}", frameIndex);
        packet->pts = frameIndex++;
        i++;
      }
      //void* buffer = CopyDataToDevice(inPacket.data, inPacket.size,
      //  runMode, DVPP);
      //if (buffer == nullptr) {
      //  E_LOG("Copy packet to device failed");
      //  av_packet_unref(&inPacket);
      //  return -1;
      //}
      
      av_packet_unref(&inPacket);

      if (!packet->data) {
        E_LOG("get packet data failed");
        return -1;
      }
      return 0;
    }

    int demux(const uint8_t* data, int len, int64_t timestamp, AclPacket& packet) {
      if (tmpPkt == nullptr) {
        tmpPkt = av_packet_alloc();
      }
      frameIndex++;
      uint8_t* h264Data = new uint8_t[BUF_SIZE];
      uint8_t* tempData = new uint8_t[BUF_SIZE];
      memcpy(tempData, data, len);

      inputPayload(tempData, len, timestamp);
      int64_t ts = 0;
      int inputLen = getH264(h264Data, ts);
      int count = 0;
      int num = 0;
      D_LOG("inputLen:{}", inputLen);
      while (inputLen) {
        int ret = av_parser_parse2(parser, codecCtx, &tmpPkt->data, &tmpPkt->size,
          h264Data, inputLen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
          E_LOG("parser wrong");
        }
        count += ret;
        inputLen -= ret;
        tmpPkt->pts = ts;
        if (tmpPkt->size) {
          num++;
          packet.data = copyDataToDevice(tmpPkt->data, tmpPkt->size, runMode, DVPP);
          if (!packet.data) {
            E_LOG("Copy packet to device failed");
            return -1;
          }
          packet.size = tmpPkt->size;
          packet.pts = tmpPkt->pts;
          av_packet_unref(tmpPkt);
        }
      }
      delete[] tempData;
      tempData = nullptr;
      if (h264Data) {
        delete[] h264Data;
        h264Data = nullptr;
      }
      return 0;
    }

    int getParserWidth() {
      if (parser)
        return parser->width;
      else {
        E_LOG("parser is NULL");
        return -1;
      }
    }

    int getParserHeight() {
      if (parser)
        return parser->height;
      else {
        E_LOG("parser is NULL");
        return -1;
      }
    }

    int getFileWidth() { return fileWidth; }
    int getFileHeight() { return fileHeight; }

  private:
    void inputPayload(const uint8_t* data, int len, int64_t timestamp) {
      D_LOG("timestamp:{}", timestamp);
      uint8_t load_hdr = data[0];
      uint8_t fu_ind = data[0];   //分片
      uint8_t fu_hdr = data[1];   //分片
      D_LOG("fu_hdr:{}", fu_hdr);

      D_LOG("load_hdr:{}", load_hdr);
      //聚合
      if ((load_hdr & 0x1f) == 24) {
        writePos = 0;
        dataLen = len - 1;//去掉load_hdr
        readPos = 1;

        while (dataLen > 0) {
          memcpy(&buff[writePos], &h264Startcode, 4);
          writePos += 4;
          uint16_t size = (data[readPos] << 8) | data[readPos + 1];
          readPos += 2;
          dataLen -= 2;
          memcpy(&buff[writePos], &data[readPos], (size_t)size);
          readPos += size;
          writePos += size;
          dataLen -= size;
        }
        outSize = writePos;
        output_h264data.push(std::make_tuple(buff, outSize, timestamp));
      }

      //单个
      else if ((load_hdr & 0x1f) > 0 && (load_hdr & 0x1f) < 24) {
        readPos = 0;
        writePos = 0;
        memcpy(&buff[writePos], &h264Startcode, 4);
        writePos += 4;
        memcpy(&buff[writePos], &data[readPos], len);
        writePos += len;
        outSize = writePos;
        output_h264data.push(std::make_tuple(buff, writePos, timestamp));
      }

      //分片
      else if (((load_hdr & 0x1f) == 28) && len >= 2) {

        dataLen = len - 2;
        readPos = 2;

        //S=1,nalu的开始
        if (fu_hdr >> 7 == 1) {
          uint8_t F = fu_ind & 0x80;
          uint8_t NRI = fu_ind & 0x60;
          uint8_t Type = fu_hdr & 0x1f;
          uint8_t nalu_hdr = F | NRI | Type;
          writePos = 0;
          memcpy(&buff[writePos], &h264Startcode, 4);
          writePos += 4;
          memcpy(&buff[writePos], &nalu_hdr, 1);
          writePos += 1;
          memcpy(&buff[writePos], &data[readPos], dataLen);
          writePos += dataLen;
        }

        //E=1,nalu的结束
        else if ((fu_hdr >> 6) == 1) {
          memcpy(&buff[writePos], &data[readPos], dataLen);
          writePos += dataLen;
          outSize = writePos;
          output_h264data.push(std::make_tuple(buff, outSize, timestamp));
        }

        //S=E=0，中间
        else if ((fu_hdr >> 5) == 0) {
          memcpy(&buff[writePos], &data[readPos], dataLen);
          writePos += dataLen;
        }
      }
      D_LOG("output_h264data.size():{}", output_h264data.size());
    }

    int getH264(uint8_t* h264Data, int64_t& timestamp) {
      if (output_h264data.empty()) return 0;
      std::tuple<uint8_t*, size_t, int64_t> firstData = output_h264data.front();
      memcpy(h264Data, std::get<0>(firstData), std::get<1>(firstData));
      timestamp = std::get<2>(firstData);
      output_h264data.pop();
      return std::get<1>(firstData);
    }

    AVCodecParserContext* parser = nullptr;
    AVCodec* codec = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVFormatContext* fmtCtx = nullptr;
    AVBSFContext* bsfCtx = nullptr;
    aclrtRunMode runMode;
    int videoStreamIndex = 0;
    int readPos = 0;   //rtp payLoad读的位置
    int writePos = 0;  //h264data写的位置
    int dataLen = 0;
    uint32_t h264Startcode = 0x01000000;
    uint8_t* buff = nullptr;
    std::queue<std::tuple<uint8_t*, size_t, int64_t>> output_h264data;
    AVPacket* tmpPkt = nullptr;
    AVPacket inPacket;
    size_t outSize = 0;
    int frameIndex = 0;
    int fileWidth = 0;
    int fileHeight = 0;
  };

  class Muxer {
  public:
    Muxer() = default;
    ~Muxer() {};

    int mux(const uint8_t* rawData, const int& rawSize, std::queue<std::vector<uint8_t>>& inData) {
      if (rawData != nullptr && rawSize) {
        H264ToRtp(rawData, rawSize, inData);
      }
      else {
        E_LOG("ERROR: encode pkt is nullptr.");
        return -1;
      }
      return 0;
    }
  private:
    void H264ToRtp(const uint8_t* rawData, const int& rawSize, std::queue<std::vector<uint8_t>>& outData) {
      uint64_t frameCount = 0;
      auto data = rawData;
      auto pktSize = rawSize;
      std::queue<std::vector<uint8_t>> outNalu;
      int sp = -1;
      int ep = -1;
      int i = 0;
      for (i = 0; i < pktSize; i++) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
          ep = i - 1;
          if (ep != -1) {
            int nalu_len = ep - sp + 1;
            std::vector<uint8_t> d;
            for (int k = 0; k < nalu_len; k++, sp++) {
              d.push_back(data[sp]);
            }
            outNalu.push(d);
            d.clear();
          }
          sp = i;
          i += 3;
        }
        else if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
          ep = i - 1;
          if (ep != -1) {
            int nalu_len = ep - sp + 1;
            std::vector<uint8_t> d;
            for (int k = 0; k < nalu_len; k++, sp++) {
              d.push_back(data[sp]);
            }
            outNalu.push(d);
            d.clear();
          }
          sp = i;
          i += 2;
        }
      }
      int res_len = pktSize - sp;
      if (res_len != 0) {
        std::vector<uint8_t> d;
        for (int k = 0; k < res_len; k++, sp++) {
          d.push_back(data[sp]);
        }
        outNalu.push(d);
        d.clear();
      }
      int nalu_size = outNalu.size();
      for (int k = 0; k < nalu_size; k++) {
        std::vector<uint8_t> nalu = outNalu.front();
        if (nalu.size()) {
          int startLen = 0;
          uint32_t startCode = 0;
          for (int i = 0; i < 4; ++i) {
            startCode = (startCode << 8) | nalu[i];
          }
          if (startCode != 1) {
            if ((startCode & 0x00000f00) == 0x00000100) {
              startLen = 3;
            }
            else {
              startLen = -1;
              throw std::runtime_error("startCode error");
            }
          }
          else {
            startLen = 4;
          }
          if (nalu.size() - startLen <= MTU + 1) {
            std::vector<uint8_t> buf(nalu.size() - startLen);
            memcpy(&buf[0], &nalu[0] + startLen, nalu.size() - startLen);
            outData.push(buf);
            buf.clear();
          }
          else {
            int index = startLen + 1;
            bool isFirst = true;
            uint8_t fuIndicator = 0xe0 & nalu[startLen];
            fuIndicator |= 0x1c;
            uint8_t fuHeader = 0x1f & nalu[startLen];
            do {
              std::vector<uint8_t> buf(MTU + 2);
              if (isFirst) {
                fuHeader |= 0x80;
                isFirst = false;
              }
              else {
                fuHeader &= 0x1f;
              }
              memset(&buf[0], 0, MTU + 2);
              memcpy(&buf[0], &fuIndicator, 1);
              memcpy(&buf[0] + 1, &fuHeader, 1);
              memcpy(&buf[0] + 2, &nalu[0] + index, MTU);
              outData.push(buf);
              buf.clear();
              index += MTU;
            } while (index < nalu.size() - MTU);
            std::vector<uint8_t> buff(nalu.size() - index + 2);
            int ssize = nalu.size() - index + 2;
            fuHeader &= 0x1f;
            fuHeader |= 0x40;
            memset(&buff[0], 0, ssize);
            memcpy(&buff[0], &fuIndicator, 1);
            memcpy(&buff[0] + 1, &fuHeader, 1);
            memcpy(&buff[0] + 2, &nalu[0] + index, nalu.size() - index);
            outData.push(buff);
            buff.clear();
          }
          nalu.clear();
          ++frameCount;
        }
        else {
          E_LOG("input_pkt is empty");
        }
        outNalu.pop();
      }
    }
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
    ~ImageReader() { close(); };

    bool open() {
      aclError aclRet = aclrtCreateStream(&stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageReader::open] Create venc stream failed, error={}", aclRet);
        return ACLE_ERROR_CREATE_STREAM;
      }

      aclrtGetRunMode(&runMode);

      channelDesc = acldvppCreateChannelDesc();
      if (channelDesc == nullptr) {
        E_LOG("[ImageReader::open] Create dvpp channel desc failed");
        return ACLE_ERROR_CREATE_DVPP_CHANNEL_DESC;
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
        return ACLE_ERRROR_CREATE_DVPP_CHANNEL;
      }

      I_LOG("[ImageReader::open] init resource success");

      return ACLE_OK;
    }

    void close() {
      if (isClose) return;

      destoryResource();

      aclError aclRet;
      if (stream_ != nullptr) {
        aclRet = aclrtDestroyStream(stream_);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::close] Vdec destroy stream failed, error={}", aclRet);
        }
        stream_ = nullptr;
      }
      
      if (channelDesc != nullptr) {
        aclRet = acldvppDestroyChannel(channelDesc);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::close] Destroy dvpp channel error={}", aclRet);
        }
        (void)acldvppDestroyChannelDesc(channelDesc);
        channelDesc = nullptr;
      }

      isClose = true;
    }

    AcleError imgread(const std::string& file, AclFrame& data) {
      return imgreadHandle(file, data);
    }

    AclFrame imgread(const std::string& file) {
      AclFrame img;
      imgreadHandle(file, img);
      return img;
    }

  protected:
    void destoryResource() {
      //if (inputPicDesc != nullptr) {
      //  (void)acldvppDestroyPicDesc(inputPicDesc);
      //  inputPicDesc = nullptr;
      //}

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

      I_LOG("get jpeg image info successed, width={}, height={}, pixel format={}, jpg format={}, predictSize={}",
        picDesc.width, picDesc.height, checkJpegFormat(picDesc.format), 
        picDesc.format, picDesc.jpegDecodeSize);

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

    AcleError initDecodeOutputDesc(const PicDesc& pic, AclFrame& inputImage) {
      auto socVersion = aclrtGetSocName();
      if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0) {
        inputImage.width = ALIGN_UP2(pic.width);
        inputImage.height = ALIGN_UP2(pic.height);
        inputImage.alignWidth = ALIGN_UP64(pic.width); // 64-byte alignment
        inputImage.alignHeight = ALIGN_UP16(pic.height); // 16-byte alignment
      }
      else {
        inputImage.width = pic.width;
        inputImage.height = pic.height;
        inputImage.alignWidth = ALIGN_UP128(pic.width); // 128-byte alignment
        inputImage.alignHeight = ALIGN_UP16(pic.height); // 16-byte alignment
      }
      if (inputImage.alignWidth == 0 || inputImage.alignHeight == 0) {
        E_LOG("Input image width {} or height {} invalid",
          inputImage.width, inputImage.height);
        return ACLE_ERROR_INVALID_ARGS;
      }

      inputImage.format = checkJpegFormat(pic.format);
      inputImage.size = pic.jpegDecodeSize;
      void* data = nullptr;
      aclError aclRet = acldvppMalloc(&data, inputImage.size);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("Malloc dvpp memory failed, error:{}", aclRet);
        return ACLE_ERROR_MALLOC_DVPP;
      }

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        E_LOG("Create dvpp pic desc failed");
        return ACLE_ERROR_CREATE_PIC_DESC;
      }
      inputImage.data = SHARED_PTR_DVPP_BUF(data);

      acldvppSetPicDescData(outputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(outputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, inputImage.alignWidth);
      acldvppSetPicDescHeightStride(outputPicDesc, inputImage.alignHeight);
      acldvppSetPicDescSize(outputPicDesc, inputImage.size);

      return ACLE_OK;
    }

    AcleError imgreadHandle(const std::string& file, AclFrame& dest) {
      PicDesc pic = { file, 0, 0 };
      uint32_t picDevBufferSize = 0;
      void* picDevBuffer = loadImageInBuffer(pic, picDevBufferSize);
      if (picDevBuffer == nullptr) {
        E_LOG("get pic device buffer failed,index is 0");
        return ACLE_ERROR;
      }

      AcleError ret = initDecodeOutputDesc(pic, dest);
      if (ret != ACL_SUCCESS) {
        E_LOG("init jpg decode output source failed, ret={}", ret);
        return ACLE_ERROR;
      }

      ret = acldvppJpegDecodeAsync(channelDesc, picDevBuffer, picDevBufferSize,
        outputPicDesc, stream_);
      if (ret != ACL_SUCCESS) {
        E_LOG("acldvppJpegDecodeAsync failed, ret={}", ret);
        return ACLE_ERROR;
      }

      ret = aclrtSynchronizeStream(stream_);
      if (ret != ACL_SUCCESS) {
        E_LOG("aclrtSynchronizeStream failed");
        return ACLE_ERROR;
      }

      return ACLE_OK;
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

    ~ImageHandler() { close(); }

    bool open(std::string mode = "") {
      aclError aclRet = aclrtCreateStream(&stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::open] Create venc stream failed, error={}", aclRet);
        return ACLE_ERROR_CREATE_STREAM;
      }

      aclrtGetRunMode(&runMode);

      channelDesc = acldvppCreateChannelDesc();
      if (channelDesc == nullptr) {
        E_LOG("[ImageHandler::open] Create dvpp channel desc failed");
        return ACLE_ERROR_CREATE_DVPP_CHANNEL_DESC;
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
        return ACLE_ERRROR_CREATE_DVPP_CHANNEL;
      }

      I_LOG("[ImageHandler::open] init resource success");

      return ACLE_OK;
    }

    void close() {
      if (isClose) return;
      aclError aclRet;
      if (stream_ != nullptr) {
        aclRet = aclrtDestroyStream(stream_);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::close] Vdec destroy stream failed, error={}", aclRet);
        }
        stream_ = nullptr;
      }

      if (channelDesc != nullptr) {
        aclRet = acldvppDestroyChannel(channelDesc);
        if (aclRet != ACL_SUCCESS) {
          E_LOG("[ImageHandler::close] Destroy dvpp channel error={}", aclRet);
        }
        (void)acldvppDestroyChannelDesc(channelDesc);
        channelDesc = nullptr;
      }

      isClose = true;
    }

    AcleError resize(const AclFrame& src, AclFrame& dest, uint32_t width, uint32_t height) {
      if (src.width == width || src.height == height) {
        E_LOG("[ImageHandler::resize] src width={}, height={} equal to target size", src.width, src.height);
        return ACLE_ERROR_DEST_INVALID;
      }
      size_.width = width;
      size_.height = height;
      AcleError ret = resizeExecute(src, dest);
      return ret;
    }

    AcleError crop(AclFrame& src, AclFrame& dest, uint32_t targetX, uint32_t targetY,
      uint32_t width , uint32_t height) {
      width = (width / 16) * 16;
      if (width <= 16) return ACLE_ERROR;
      if (targetX < 0 || targetY < 0) {
        E_LOG("[ImageHandler::crop] input crop x={} y={} failed", targetX, targetY);
        return ACLE_ERROR;
      }
      if (targetX + width > src.width || targetY + height > src.height) {
        E_LOG("[ImageHandler::crop] input crop x={} + width={} ? src width={} "
          "and y={} + height={} ? src height={} failed", targetX, width, src.width, targetY, height, src.height);
        return ACLE_ERROR;
      }
      cropOutSize_.width = width;
      cropOutSize_.height = height;
      AcleError ret = cropHandle(src, dest, targetX, targetY, width, height);
      return ret;
    }

    AcleError overlay(const AclFrame& src1, AclFrame& dest, uint32_t targetX, uint32_t targetY) {
      if (src1.width <= 16) return ACLE_ERROR;
      return pasteHandle(src1, dest, targetX, targetY);
    }

    AcleError cvtColor(const AclImage& src, AclImage& dest) {
      return convertHandle(src, dest);
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
    AcleError initResizeInputDesc(const AclFrame& inputImage) {
      uint32_t alignWidth = inputImage.alignWidth;
      uint32_t alignHeight = inputImage.alignHeight;
      if (alignWidth == 0 || alignHeight == 0) {
        E_LOG("[ImageHandler::initResizeInputDesc] Input image width={} or height={} invalid",
          inputImage.width, inputImage.height);
        return ACLE_ERROR_INVALID_ARGS;
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
        return ACLE_ERROR_CREATE_PIC_DESC;
      }

      acldvppSetPicDescData(inputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, ALIGN_UP2(inputImage.width));
      acldvppSetPicDescHeight(inputPicDesc, ALIGN_UP2(inputImage.height));
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputBufferSize);

      return ACLE_OK;
    }

    AcleError initResizeOutputDesc() {
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
        return ACLE_ERROR_INVALID_ARGS;
      }

      outDevBufSize = YUV420SP_SIZE(resizeOutWidthStride, resizeOutHeightStride);
      aclError aclRet = acldvppMalloc(&outDevBuf, outDevBufSize);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::initResizeOutputDesc] Dvpp resize malloc output buffer failed, "
          "size={}, error={}", outDevBufSize, aclRet);
        return ACLE_ERROR_MALLOC_DVPP;
      }

      outputPicDesc = acldvppCreatePicDesc();
      if (!outputPicDesc) {
        E_LOG("[ImageHandler::initResizeOutputDesc] acldvppCreatePicDesc vpcOutputDesc_ failed");
        return ACLE_ERROR_CREATE_PIC_DESC;
      }

      acldvppSetPicDescData(outputPicDesc, outDevBuf);
      acldvppSetPicDescFormat(outputPicDesc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
      acldvppSetPicDescWidth(outputPicDesc, resizeOutWidth);
      acldvppSetPicDescHeight(outputPicDesc, resizeOutHeight);
      acldvppSetPicDescWidthStride(outputPicDesc, resizeOutWidthStride);
      acldvppSetPicDescHeightStride(outputPicDesc, resizeOutHeightStride);
      acldvppSetPicDescSize(outputPicDesc, outDevBufSize);

      return ACLE_OK;
    }

    AcleError initResizeResource(const AclFrame& inputImage) {
      resizeConfig_ = acldvppCreateResizeConfig();
      if (resizeConfig_ == nullptr) {
        E_LOG("[ImageHandler::initResizeResource] Dvpp resize init failed for create config failed");
        return ACLE_ERROR_CREATE_RESIZE_CONFIG;
      }

      AcleError ret = initResizeInputDesc(inputImage);
      if (ret != ACLE_OK) {
        E_LOG("[ImageHandler::initResizeResource] InitResizeInputDesc failed");
        return ret;
      }

      ret = initResizeOutputDesc();
      if (ret != ACLE_OK) {
        E_LOG("[ImageHandler::initResizeResource] InitResizeOutputDesc failed");
        return ret;
      }

      return ACLE_OK;
    }

    void destroyResizeResource() {
      if (resizeConfig_ != nullptr) {
        (void)acldvppDestroyResizeConfig(resizeConfig_);
        resizeConfig_ = nullptr;
      }

      destoryResource();
    }

    AcleError resizeExecute(const AclFrame& srcImage, AclFrame& resizedImage) {
      AcleError atlRet = initResizeResource(srcImage);
      if (atlRet != ACLE_OK) {
        E_LOG("Dvpp resize failed for init error");
        return atlRet;
      }

      // resize pic
      aclError aclRet = acldvppVpcResizeAsync(channelDesc, inputPicDesc,
        outputPicDesc, resizeConfig_, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("acldvppVpcResizeAsync failed, error:{}", aclRet);
        return ACLE_ERROR_RESIZE_ASYNC;
      }

      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("resize aclrtSynchronizeStream failed, error:{}", aclRet);
        return ACLE_ERROR_SYNC_STREAM;
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

      return ACLE_OK;
    }

    //裁剪/叠加功能实现

    //裁剪/叠加功能输入图片描述初始化
    AcleError initCropOrPasteInputDesc(const AclFrame& inputImage) {
      uint32_t alignWidth = 0;
      uint32_t alignHeight = 0;
      if (inputImage.format == PIXEL_FORMAT_BGR_888) {
        alignWidth = ALIGN_UP16(inputImage.width) * 3;
        alignHeight = ALIGN_UP2(inputImage.height);
      }
      else if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
        alignWidth = ALIGN_UP16(inputImage.width);
        alignHeight = ALIGN_UP2(inputImage.height);
      }
      if (alignWidth == 0 || alignHeight == 0) {
        E_LOG("Invalid image parameters, width={}, height={}",
          inputImage.width, inputImage.height);
        return ACLE_ERROR;
      }

      //uint32_t inputBufferSize = 0;
      //if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
      //  inputBufferSize = YUV420SP_SIZE(alignWidth, alignHeight);
      //}
      //else if (inputImage.format == PIXEL_FORMAT_RGB_888) {
      //  inputBufferSize = RGBU8_IMAGE_SIZE(alignWidth, alignHeight);
      //}
      //else {
      //  E_LOG("Dvpp only support yuv and rgb format.");
      //  return ACLE_ERROR;
      //}

      if (inputImage.data == nullptr) {
        E_LOG("input image data is nullptr");
        acldvppFree(inputBuffer);
        inputBuffer = nullptr;
        return ACLE_ERROR;
      }

      if (inputImage.size == 0) {
        E_LOG("input image size {}", inputImage.size);
        acldvppFree(inputBuffer);
        inputBuffer = nullptr;
        return ACLE_ERROR;
      }

      //aclError ret = acldvppMalloc(&inputBuffer, inputImage.size);
      //ret = aclrtMemcpy(inputBuffer, inputImage.size, inputImage.data, inputImage.size,
      //  ACLE_MEMCPY_DEVICE_TO_DEVICE); 
      //if (ret != ACL_SUCCESS) {
      //  E_LOG("memcpy failed. raw size is {}, Input buffer size is {}, ret={}",
      //    inputImage.size, inputImage.size, ret);
      //  acldvppFree(inputBuffer);
      //  inputBuffer = nullptr;
      //  return ACLE_ERROR;
      //}

      inputPicDesc = acldvppCreatePicDesc();
      if (inputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLE_ERROR;
      }
      D_LOG("paste w:{}/h:{} wstride:{}/hstride:{} format:{}, size:{}",
        inputImage.width, inputImage.height, alignWidth, alignHeight, inputImage.format, inputImage.size);

      acldvppSetPicDescData(inputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(inputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputImage.size);

      return ACLE_OK;
    }

    //裁剪输出图片描述初始化
    AcleError initCropOutputDesc(AclFrame& inputImage) {
      inputImage.width = cropOutSize_.width;
      inputImage.height = cropOutSize_.height;
      inputImage.alignWidth = ALIGN_UP16(inputImage.width);
      inputImage.alignHeight = ALIGN_UP2(inputImage.height);
      inputImage.format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
      if (inputImage.alignWidth == 0 || inputImage.alignHeight == 0) {
        E_LOG("[ImageHandler::initCropOutputDesc] image width={} and height={} failed",
          inputImage.width, inputImage.height);
        return ACLE_ERROR;
      }

      inputImage.size = YUV420SP_SIZE(inputImage.alignWidth, inputImage.alignHeight);
      void* data = nullptr;
      aclError aclRet = acldvppMalloc(&data, inputImage.size);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::initCropOutputDesc] Dvpp crop malloc output memory failed, crop "
          "width={}, height={} size={}, error={}",
          cropOutSize_.width, cropOutSize_.height, inputImage.size, aclRet);
        return ACLE_ERROR;
      }
      aclrtMemset(data, inputImage.size, 0, inputImage.size);
      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLE_ERROR;
      }
      inputImage.data = SHARED_PTR_DVPP_BUF(data);
      acldvppSetPicDescData(outputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(outputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, inputImage.alignWidth);
      acldvppSetPicDescHeightStride(outputPicDesc, inputImage.alignHeight);
      acldvppSetPicDescSize(outputPicDesc, inputImage.size);

      return ACLE_OK;
    }

    //叠加输出图片描述初始化
    AcleError initPasteOutputDesc(AclFrame& inputImage) {
      uint32_t widthStride = ALIGN_UP16(inputImage.width);
      uint32_t heightStride = ALIGN_UP2(inputImage.height);
      if (!inputImage.data) {
        E_LOG("[ImageHandler::initPasteOutputDesc] bottom img can not be nullptr");
        return ACLE_ERROR;
      }
      
      if (inputImage.size == 0) {
        E_LOG("[ImageHandler::initPasteOutputDesc] bottom img size can not be zero");
        return ACLE_ERROR;
      }

      if (inputImage.width == 0 || inputImage.height == 0) {
        E_LOG("[ImageHandler::initPasteOutputDesc] bottom img width({}) and height({}) is invaild",
          inputImage.width, inputImage.height);
        return ACLE_ERROR;
      }

      uint32_t outputBufferSize = YUV420SP_SIZE(widthStride, heightStride);
      //auto aclRet = acldvppMalloc(&outputBuffer, outputBufferSize);
      //aclRet = aclrtMemcpy(outputBuffer, outputBufferSize, inputImage.data.get(), inputImage.size, ACLE_MEMCPY_DEVICE_TO_DEVICE);
      //if (aclRet != ACL_SUCCESS) {
      //  E_LOG("acl memcpy data to dev failed, image.size={}, ret={}", outputBufferSize, aclRet);
      //  (void)acldvppFree(outputBuffer);
      //  outputBuffer = nullptr;
      //  return false;
      //}
      D_LOG("paste w:{}/h:{} wstride:{}/hstride:{} format:{}, size:{}",
        inputImage.width, inputImage.height, inputImage.alignWidth, inputImage.alignHeight, inputImage.format, inputImage.size);

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLE_ERROR;
      }
      acldvppSetPicDescData(outputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(outputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, inputImage.alignWidth);
      acldvppSetPicDescHeightStride(outputPicDesc, inputImage.alignHeight);
      acldvppSetPicDescSize(outputPicDesc, inputImage.size);

      return ACLE_OK;
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

    AcleError cropHandle(AclFrame& topImg, AclFrame& destImg,
      uint32_t targetX, uint32_t targetY, uint32_t width, uint32_t height) {
      //初始化输入图片信息描述
      if (initCropOrPasteInputDesc(topImg) != ACLE_OK) {
        return ACLE_ERROR;
      }
      //计算裁剪ROI区域
      //必须为偶数
      uint32_t cropLeftOffset = targetX - (targetX & 1); //相对输入图片的左偏移
      uint32_t cropTopOffset = targetY - (targetY & 1); //相对输入图片的上偏移
      //必须为奇数
      uint32_t cropRightOffset = (targetX + width) - (((targetX + width) & 1) ^ 1);  //相对输入图片的右偏移
      uint32_t cropBottomOffset = (targetY + height) - (((targetY + height) & 1) ^ 1); //相对输入图片的下偏移

      //设置输入图片裁剪的ROI区域
      cropArea_ = acldvppCreateRoiConfig(cropLeftOffset, cropRightOffset,
        cropTopOffset, cropBottomOffset);
      if (cropArea_ == nullptr) {
        E_LOG("[ImageHandler::cropHandle] acldvppCreateRoiConfig cropArea_ failed");
        return ACLE_ERROR;
      }

      //初始化输出图片信息描述
      if (initCropOutputDesc(destImg) != ACLE_OK) {
        return ACLE_ERROR;
      }

      //计算叠加ROI区域
      // 必须为偶数
      // 左偏移必须满足16对齐
      uint32_t pasteLeftOffset = 0;
      uint32_t pasteTopOffset = 0;

      // 必须为奇数
      uint32_t pasteRightOffset = pasteLeftOffset + destImg.width;
      pasteRightOffset = pasteRightOffset - ((pasteRightOffset & 1) ^ 1);
      uint32_t pasteBottomOffset = pasteTopOffset + destImg.height;
      pasteBottomOffset = pasteBottomOffset - ((pasteBottomOffset & 1) ^ 1);

      pasteArea_ = acldvppCreateRoiConfig(pasteLeftOffset, pasteRightOffset,
        pasteTopOffset, pasteBottomOffset);
      if (pasteArea_ == nullptr) {
        E_LOG("[ImageHandler::cropHandle] acldvppCreateRoiConfig pasteArea_ failed");
        return ACLE_ERROR;
      }

      aclError aclRet = acldvppVpcCropAndPasteAsync(channelDesc, inputPicDesc,
        outputPicDesc, cropArea_, pasteArea_, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::cropHandle] acldvppVpcCropAndPasteAsync failed, aclRet={}", aclRet);
        return ACLE_ERROR;
      }

      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::cropHandle] use aclrtSynchronizeStream failed, aclRet={}", aclRet);
        return ACLE_ERROR;
      }

      destroyCropOrPasteResource();

      return ACLE_OK;
    }

    AcleError pasteHandle(const AclFrame& topImg, AclFrame& destImg, uint32_t targetX, uint32_t targetY) {
      //初始化输入图片信息描述
      if (initCropOrPasteInputDesc(topImg) != ACLE_OK) {
        return ACLE_ERROR;
      }

      //若右偏移或下偏移大于被贴对象的宽度或高度，则需要裁剪
      //计算裁剪ROI区域
      //必须为偶数
      uint32_t cropLeftOffset = 0; //相对输入图片的左偏移
      uint32_t cropTopOffset = 0; //相对输入图片的上偏移
      
      //必须为奇数
      uint32_t cropRightOffset = topImg.width - ((topImg.width & 1) ^ 1);  //相对输入图片的右偏移
      uint32_t cropBottomOffset = topImg.height - ((topImg.height & 1) ^ 1); //相对输入图片的下偏移

      //设置输入图片裁剪的ROI区域
      cropArea_ = acldvppCreateRoiConfig(cropLeftOffset, cropRightOffset,
        cropTopOffset, cropBottomOffset);
      if (cropArea_ == nullptr) {
        E_LOG("acldvppCreateRoiConfig cropArea_ failed");
        return ACLE_ERROR;
      }

      //初始化输出图片信息描述
      if (initPasteOutputDesc(destImg) != ACLE_OK) {
        return ACLE_ERROR;
      }

      //计算叠加ROI区域
      // 必须为偶数
      // 左偏移必须满足16对齐
      uint32_t pasteLeftOffset = (targetX / 16) * 16;
      uint32_t pasteTopOffset = targetY - (targetY & 1);

      // 必须为奇数
      uint32_t pasteRightOffset = pasteLeftOffset + topImg.width;
      pasteRightOffset = pasteRightOffset - ((pasteRightOffset & 1) ^ 1);
      uint32_t pasteBottomOffset = pasteTopOffset + topImg.height;
      pasteBottomOffset = pasteBottomOffset - ((pasteBottomOffset & 1) ^ 1);
      
      pasteArea_ = acldvppCreateRoiConfig(pasteLeftOffset, pasteRightOffset,
        pasteTopOffset, pasteBottomOffset);
      if (pasteArea_ == nullptr) {
        E_LOG("acldvppCreateRoiConfig pasteArea_ failed");
        return ACLE_ERROR;
      }

      aclError aclRet = acldvppVpcCropAndPasteAsync(channelDesc, inputPicDesc,
        outputPicDesc, cropArea_, pasteArea_, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::pasteHandle] acldvppVpcCropAndPasteAsync failed, aclRet={}", aclRet);
        return ACLE_ERROR;
      }

      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::pasteHandle] use aclrtSynchronizeStream failed, aclRet={}", aclRet);
        return ACLE_ERROR;
      }

      destroyCropOrPasteResource();
      
      return ACLE_OK;
    }

    //格式转换输入图片描述初始化
    AcleError initConvertpicDesc(const AclImage& inputImage, AclImage& outputImage) {
      uint32_t alignWidth = ALIGN_UP16(inputImage.width);
      uint32_t alignHeight = ALIGN_UP2(inputImage.height);
      if (alignWidth == 0 || alignHeight == 0) {
        E_LOG("Invalid image parameters, width={}, height={}",
          inputImage.width, inputImage.height);
        return ACLE_ERROR;
      }

      uint32_t inputBufferSize = 0;
      if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
        inputBufferSize = YUV420SP_SIZE(alignWidth, alignHeight);
      }
      else if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_444) {
        inputBufferSize = YUV444SP_SIZE(alignWidth, alignHeight);
      }
      else if (inputImage.format == PIXEL_FORMAT_RGB_888) {
        inputBufferSize = RGBU8_IMAGE_SIZE(alignWidth, alignHeight);
      }
      else {
        E_LOG("Dvpp only support yuv and rgb format.");
        return ACLE_ERROR;
      }

      if (inputImage.data == nullptr) {
        E_LOG("input image data is nullptr");
        acldvppFree(inputBuffer);
        inputBuffer = nullptr;
        return ACLE_ERROR;
      }

      if (inputImage.size == 0) {
        E_LOG("input image size {}", inputImage.size);
        acldvppFree(inputBuffer);
        inputBuffer = nullptr;
        return ACLE_ERROR;
      }

      inputPicDesc = acldvppCreatePicDesc();
      if (inputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLE_ERROR;
      }
      I_LOG("convert intput w:{}/h:{} wstride:{}/hstride:{} format:{}, size:{}",
        inputImage.width, inputImage.height, alignWidth, alignHeight, inputImage.format, inputBufferSize);

      acldvppSetPicDescData(inputPicDesc, inputImage.data);
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(inputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputBufferSize);

      uint32_t outputBufferSize = YUV420SP_SIZE(alignWidth, alignHeight);
      I_LOG("convert output w:{}/h:{} wstride:{}/hstride:{} format:{}, size:{}",
        inputImage.width, inputImage.height, alignWidth, alignHeight, PIXEL_FORMAT_YUV_SEMIPLANAR_420, outputBufferSize);


      aclError aclRet = acldvppMalloc(&outputImage.data, outputBufferSize);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("Dvpp crop malloc output memory failed, crop "
          "width {}, height {} size {}, error {}",
          cropOutSize_.width, cropOutSize_.height,
          outputBufferSize, aclRet);
        return ACLE_ERROR;
      }
      outputImage.width = inputImage.width;
      outputImage.height = inputImage.height;
      outputImage.widthStride = alignWidth;
      outputImage.heightStride = alignHeight;
      outputImage.size = inputBufferSize;
      outputImage.format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLE_ERROR;
      }
      acldvppSetPicDescData(outputPicDesc, outputImage.data);
      acldvppSetPicDescFormat(outputPicDesc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(outputPicDesc, alignHeight);
      acldvppSetPicDescSize(outputPicDesc, outputBufferSize);
      return ACLE_OK;
    }

    AcleError convertHandle(const AclImage& src, AclImage& dest) {
      if (initConvertpicDesc(src, dest) != ACLE_OK) {
        E_LOG("[ImageHandler::convertHandle] init input and output picdesc failed");
        return ACLE_ERROR;
      }

      aclError aclRet = acldvppVpcConvertColorAsync(channelDesc, inputPicDesc, outputPicDesc, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::convertHandle] acldvppVpcConvertColorAsync failed, aclRet={}", aclRet);
        return ACLE_ERROR;
      }
      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::convertHandle] aclrtSynchronizeStream failed, aclRet={}", aclRet);
        return ACLE_ERROR;
      }

      destoryResource();
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