#pragma once
#include "core/Types.h"
#include "core/Utils.hpp"
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
  class Decoder23 {
  public:
    Decoder23(std::string streamName, int32_t devId, aclrtContext aclCtx) :
      aclLiteVideoProc(nullptr),
      streamName(streamName),
      deviceId(devId),
      context(aclCtx) {
      ACLLITE_LOG_INFO("[Decoder::Decoder] Decoder is create, streamName=%s, devId=%d", streamName.c_str(), devId);
    }

    ~Decoder23() {
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

  const int64_t kUsec = 1000000;
  const uint32_t kDecodeFrameQueueSize = 256;
  const int kDecodeQueueOpWait = 10000; // decode wait 10ms/frame
  const int kFrameEnQueueRetryTimes = 1000; // max wait time for the frame to enter in queue
  const int kQueueOpRetryTimes = 1000;
  const int kOutputJamWait = 10000;
  const int kInvalidTpye = -1;
  const int kWaitDecodeFinishInterval = 1000;
  const int kDefaultFps = 1;
  const int kReadSlow = 5;
  const uint32_t kVideoChannelMax310 = 32;
  const uint32_t kVideoChannelMax310B = 128;
  const uint32_t kVideoChannelMax310P = 256;

  const int kNoFlag = 0; // no flag
  const int kInvalidVideoIndex = -1; // invalid video index
  const std::string kRtspTransport = "rtsp_transport"; // rtsp transport
  const std::string kUdp = "udp"; // video format udp
  const std::string kTcp = "tcp";
  const std::string kBufferSize = "buffer_size"; // buffer size string
  const std::string kMaxBufferSize = "10485760"; // maximum buffer size:10MB
  const std::string kMaxDelayStr = "max_delay"; // maximum delay string
  const std::string kMaxDelayValue = "100000000"; // maximum delay time:100s
  const std::string kTimeoutStr = "stimeout"; // timeout string
  const std::string kTimeoutValue = "5000000"; // timeout:5s
  const std::string kPktSize = "pkt_size"; // ffmpeg pakect size string
  const std::string kPktSizeValue = "10485760"; // ffmpeg packet size value:10MB
  const std::string kReorderQueueSize = "reorder_queue_size"; // reorder queue size
  const std::string kReorderQueueSizeValue = "0"; // reorder queue size value
  const int kErrorBufferSize = 1024; // buffer size for error info
  const uint32_t kDefaultStreamFps = 5;
  const uint32_t kOneSecUs = 1000 * 1000;

  class Decoder {
  public:
    Decoder(CodecFormat& fmt) : decodeCtx(fmt) {};

    ~Decoder() {};

    bool open() {
      if (isVideoFile(decodeCtx.file)) {
        if (!fileIsExist(decodeCtx.file)) {
          E_LOG("[Decoder::open] file {} is not find", decodeCtx.file);
          return true;
        }

        return initSource();
      }
    };

    void close() {};

    int readFrame(ImageData& data) {
      return 0;
    };

  private:
    bool fileIsExist(const std::string& path) {
      std::ifstream file(path);
      if (!file) return false;
      return true;
    }

    bool isVideoFile(const std::string& str) {
      std::regex regexVideoFile(RegexVideoFile.c_str());
      return regex_match(str, regexVideoFile);
    }

    void setDictForRtsp(AVDictionary*& avdic) {
      T_LOG("Set parameters for {}", decodeCtx.file);

      av_dict_set(&avdic, kRtspTransport.c_str(), kTcp.c_str(), kNoFlag);
      av_dict_set(&avdic, kBufferSize.c_str(), kMaxBufferSize.c_str(), kNoFlag);
      av_dict_set(&avdic, kMaxDelayStr.c_str(), kMaxDelayValue.c_str(), kNoFlag);
      av_dict_set(&avdic, kTimeoutStr.c_str(), kTimeoutValue.c_str(), kNoFlag);
      av_dict_set(&avdic, kReorderQueueSize.c_str(),
        kReorderQueueSizeValue.c_str(), kNoFlag);
      av_dict_set(&avdic, kPktSize.c_str(), kPktSizeValue.c_str(), kNoFlag);
      T_LOG("Set parameters for {} end", decodeCtx.file);
    }

    bool openVideo(AVFormatContext*& avFormatContext) {
      bool ret = true;
      AVDictionary* avdic = nullptr;

      av_log_set_level(AV_LOG_DEBUG);

      D_LOG("Open video {} ...", decodeCtx.file);
      setDictForRtsp(avdic);
      int openRet = avformat_open_input(&avFormatContext,
        decodeCtx.file.c_str(), nullptr,
        &avdic);
      if (openRet < 0) { // check open video result
        char buf_error[kErrorBufferSize];
        av_strerror(openRet, buf_error, kErrorBufferSize);

        E_LOG("Could not open video:{}, return:{}, error info:{}",
          decodeCtx.file, openRet, buf_error);
        ret = false;
      }

      if (avdic != nullptr) { // free AVDictionary
        av_dict_free(&avdic);
      }

      return ret;
    }

    int getVideoIndex(AVFormatContext* avFormatContext) {
      if (avFormatContext == nullptr) { // verify input pointer
        return kInvalidVideoIndex;
      }

      // get video index in streams
      for (uint32_t i = 0; i < avFormatContext->nb_streams; i++) {
        if (avFormatContext->streams[i]->codecpar->codec_type
          == AVMEDIA_TYPE_VIDEO) { // check is media type is video
          return i;
        }
      }

      return kInvalidVideoIndex;
    }

    AclLiteError getVideoInfo() {
      avformat_network_init(); // init network
      AVFormatContext* avFormatContext = avformat_alloc_context();
      bool ret = openVideo(avFormatContext);
      if (ret == false) {
        E_LOG("Open %s failed", decodeCtx.file);
        return ACLLITE_ERROR;
      }

      if (avformat_find_stream_info(avFormatContext, NULL) < 0) {
        E_LOG("Get stream info of {} failed", decodeCtx.file);
        return;
      }

      int videoIndex = getVideoIndex(avFormatContext);
      if (videoIndex == kInvalidVideoIndex) { // check video index is valid
        E_LOG("Video index is {}, current media stream has no "
          "video info:{}",
          kInvalidVideoIndex, decodeCtx.file);
        avformat_close_input(&avFormatContext);
        return;
      }

      AVStream* inStream = avFormatContext->streams[videoIndex];

      decodeCtx.width = inStream->codecpar->width;
      decodeCtx.height = inStream->codecpar->height;
      decodeCtx.fps = 25;
      if (inStream->avg_frame_rate.den) {
        decodeCtx.fps = inStream->avg_frame_rate.num / inStream->avg_frame_rate.den;
      }

      int videoType_ = inStream->codecpar->codec_id;
      int profile_ = inStream->codecpar->profile;

      avformat_close_input(&avFormatContext);

      I_LOG("Video %s, type %d, profile %d, width:%d, height:%d, fps:%d",
        decodeCtx.file, videoType_, profile_, decodeCtx.width, decodeCtx.height, decodeCtx.fps);
      return;
    }

    AclLiteError frameImageEnQueue(std::shared_ptr<AclFrame> frameData) {
      for (int count = 0; count < kFrameEnQueueRetryTimes; count++) {
        if (frameQueue.Push(frameData)) return ACLLITE_OK;
        usleep(kDecodeQueueOpWait);
      }
      E_LOG("Video %s lost decoded image for queue full", decodeCtx.file);

      return ACLLITE_ERROR_VDEC_QUEUE_FULL;
    }

    static void callback(acldvppStreamDesc* input, acldvppPicDesc* output, void* userData) {
      Decoder* own = (Decoder*)userData;
      if (!own->isWork.load()) {
        return;
      }
      // Get decoded image parameters
      std::shared_ptr<AclFrame> frame = std::make_shared<AclFrame>();
      frame->format = acldvppGetPicDescFormat(output);
      frame->width = acldvppGetPicDescWidth(output);
      frame->height = acldvppGetPicDescHeight(output);
      frame->alignWidth = acldvppGetPicDescWidthStride(output);
      frame->alignHeight = acldvppGetPicDescHeightStride(output);
      frame->size = acldvppGetPicDescSize(output);

      void* vdecOutBufferDev = acldvppGetPicDescData(output);
      frame->data = SHARED_PTR_DVPP_BUF(vdecOutBufferDev);

      // Put the decoded image to queue for read
      own->frameImageEnQueue(frame);
      // Release resouce
      aclError ret = acldvppDestroyPicDesc(output);
      if (ret != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("fail to destroy pic desc, error %d", ret);
      }

      if (input != nullptr) {
        void* inputBuf = acldvppGetStreamDescData(input);
        if (inputBuf != nullptr) {
          acldvppFree(inputBuf);
        }
        aclError ret = acldvppDestroyStreamDesc(input);
        if (ret != ACL_SUCCESS) {
          ACLLITE_LOG_ERROR("fail to destroy input stream desc");
        }
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
        ACLLITE_LOG_ERROR("Video decoder set context failed, error: %d", ret);
      }

      own->isWork.store(true);
      while (own->isWork.load()) {
        aclrtProcessReport(1);
      }

      ACLLITE_LOG_INFO("Vdec subscribe thread exit!");

      return (void*)ACLLITE_OK;
    }

    AclLiteError createVdecChannel() {
      vdecChannelDesc_ = aclvdecCreateChannelDesc();
      if (vdecChannelDesc_ == nullptr) {
        E_LOG("Create vdec channel desc failed");
        return ACLLITE_ERROR_CREATE_DVPP_CHANNEL_DESC;
      }

      // 暂不设置通道id
      //aclError ret = aclvdecSetChannelDescChannelId(vdecChannelDesc_,
      //  channelId_);
      //if (ret != ACL_SUCCESS) {
      //  ACLLITE_LOG_ERROR("Set vdec channel id to %d failed, errorno:%d",
      //    channelId_, ret);
      //  return ACLLITE_ERROR_SET_VDEC_CHANNEL_ID;
      //}

      aclError ret = aclvdecSetChannelDescThreadId(vdecChannelDesc_, threadId_);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel thread id failed, errorno:{}", ret);
        return ACLLITE_ERROR_SET_VDEC_CHANNEL_THREAD_ID;
      }

      // callback func
      ret = aclvdecSetChannelDescCallback(vdecChannelDesc_, callback);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel callback failed, errorno:{}", ret);
        return ACLLITE_ERROR_SET_VDEC_CALLBACK;
      }

      ret = aclvdecSetChannelDescEnType(vdecChannelDesc_, H264_BASELINE_LEVEL);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel entype failed, errorno:{}", ret);
        return ACLLITE_ERROR_SET_VDEC_ENTYPE;
      }

      ret = aclvdecSetChannelDescOutPicFormat(vdecChannelDesc_, decodeCtx.format);
      if (ret != ACL_SUCCESS) {
        E_LOG("Set vdec channel pic format failed, errorno:{}", ret);
        return ACLLITE_ERROR_SET_VDEC_PIC_FORMAT;
      }

      // create vdec channel
      ACLLITE_LOG_INFO("Start create vdec channel by desc...");
      ret = aclvdecCreateChannel(vdecChannelDesc_);
      if (ret != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("fail to create vdec channel");
        return ACLLITE_ERROR_CREATE_VDEC_CHANNEL;
      }
      ACLLITE_LOG_INFO("Create vdec channel ok");

      return ACLLITE_OK;
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

      aclRet = getVideoInfo();
      if (aclRet != ACL_SUCCESS) {
        return false;
      }

      aclError aclRet = aclrtCreateStream(&stream_);
      if (aclRet != ACL_SUCCESS) {
        ACLLITE_LOG_ERROR("Vdec create stream failed, errorno:%d", aclRet);
        return ACLLITE_ERROR_CREATE_STREAM;
      }
      ACLLITE_LOG_INFO("Vdec create stream ok");

      int ret = pthread_create(&threadId_, nullptr, notifyCallbackFunc, (void*)this);
      if (ret) {
        E_LOG("[Decoder::initResource] Create notify callback thread failed, errorCode={}", ret);
        return ACLLITE_ERROR_CREATE_THREAD;
      }
      (void)aclrtSubscribeReport(static_cast<uint64_t>(threadId_), stream_);

      ret = createVdecChannel();
      if (ret != ACLLITE_OK) {
        ACLLITE_LOG_ERROR("Create vdec channel failed");
        return ret;
      }

      return ACLLITE_OK;
    }

    AclLiteError createInputStreamDesc(std::shared_ptr<AclFrame> frameData) {
      return ACLLITE_OK;
    }

    AclLiteError createOutputPicDesc(size_t size) {
      return ACLLITE_OK;
    }

    AclLiteError process(std::shared_ptr<FrameData> frameData, void* userData) {
      return ACLLITE_OK;
    }

    CodecFormat decodeCtx;
    pthread_t threadId_;
    aclvdecChannelDesc* vdecChannelDesc_ = nullptr;
    acldvppPicDesc* inputPicDesc_ = nullptr;
    aclrtStream stream_ = nullptr;
    SafeQueue<std::shared_ptr<AclFrame>> frameQueue{};
    std::atomic<bool> isWork{ false };
    bool needClose = false;
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
        data = CopyDataToHost(vencData, size, encodeCtx.runMode, NORMAL);
      }
      size_t ret = fwrite(data, 1, size, outFp_);
      if (ret != size) {
        E_LOG("[Encoder::saveVencFile] Save venc file {} failed, need write {} bytes, "
          "but only write {} bytes, error: {}",
          encodeCtx.file, size, ret, strerror(errno));
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
        data = CopyDataToHost(data, size, ACL_HOST, NORMAL);
        AclPacket pkt;
        pkt.data = (uint8_t*)data;
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
        if (data) {
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

#define INBUF_SIZE 4096
#define BUF_SIZE 1024 * 1024
#define MTU 1300
  class Demuxer {
  public:
    Demuxer(AVCodecContext* ctx) {
      codecCtx = ctx;
    };

    ~Demuxer() {
      close();
    }

    bool open(std::string file = "") {
      if (!file.empty()) {
        int err;
        fmt_ctx = avformat_alloc_context();
        if ((err = avformat_open_input(&fmt_ctx, file.c_str(), NULL, NULL)) < 0) {
          throw std::runtime_error("ERROR: avformat_open_input error, open input file.");
          close();
          return err;
        }

        if ((err = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
          throw std::runtime_error("ERROR: avformat_find_stream_info error, find stream information failed.");
          close();
          return err;
        }

        /* select the video stream */
        err = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, 0, -1, &codec, 0);
        if (err < 0) {
          throw std::runtime_error("ERROR: av_find_best_stream error, find video stream failed.");
          close();
          return err;
        }
        video_stream_index = err;

        /* create decoding context */
        if (!(codecCtx = avcodec_alloc_context3(codec))) {
          close();
          return AVERROR(ENOMEM);
        }

        if (avcodec_parameters_to_context(codecCtx, fmt_ctx->streams[video_stream_index]->codecpar) < 0) {
          throw std::runtime_error("ERROR: avcodec_parameters_to_context error, add codec param to codec failed.");
        }

        /* init the video decoder */
        if ((err = avcodec_open2(codecCtx, codec, NULL)) < 0) {
          throw std::runtime_error("ERROR: avcodec_open2 error, open video decoder failed.");
          close();
          return err;
        }
      }
      else {
        codec = avcodec_find_decoder_by_name("h264");
        if (!codec) {
          throw std::runtime_error("Codec not found");
        }
        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) {
          E_LOG("[Demuxer::open] codecCtx cannot be nullptr!");
          return false;
        }
        buff = new uint8_t[BUF_SIZE];
        parser = av_parser_init(AV_CODEC_ID_H264);
        if (!parser) {
          E_LOG("[Demuxer::open] use av_parser_init failed");
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
      if (buff) {
        delete[] buff;
        buff = nullptr;
      }
      if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
      }
      if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
      }
    }

    int demux(AclPacket* packet) {
      int err = 0;
      AVPacket inPacket;
      if ((err = av_read_frame(fmt_ctx, &inPacket)) < 0) {
        if (err == AVERROR_EOF) {//如果读到文件尾，返回-3，下次调用重新从文件头开始读
          D_LOG("WARNING: av_read_frame warn, no more frame to read.");
          avformat_close_input(&fmt_ctx);
          fmt_ctx = nullptr;
          return -3;
        }
        else {
          throw std::runtime_error("ERROR: av_read_frame error");
          return -1;
        }
      }
      if (inPacket.stream_index != video_stream_index) return -2;
      void* buffer = CopyDataToDevice(inPacket.data, inPacket.size,
        runMode, DVPP);
      if (buffer == nullptr) {
        ACLLITE_LOG_ERROR("Copy frame h26x data to dvpp failed");
        return ACLLITE_ERROR_COPY_DATA;
      }
      packet->data = (uint8_t*)buffer;
      packet->size = inPacket.size;
      packet->pts = inPacket.pts;
      av_packet_unref(&inPacket);
      return 0;
    }

    int demux(const uint8_t* data, int len, int64_t timestamp, AclPacket* packet) {
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
        int ret = av_parser_parse2(parser, codecCtx, &packet->data, &packet->size,
          h264Data, inputLen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
          E_LOG("parser wrong");
        }
        count += ret;
        inputLen -= ret;
        packet->pts = ts;
        if (packet->size) num++;
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
    AVFormatContext* fmt_ctx = nullptr;
    aclrtRunMode runMode;
    int video_stream_index = 0;
    int readPos = 0;   //rtp payLoad读的位置
    int writePos = 0;  //h264data写的位置
    int dataLen = 0;
    uint32_t h264Startcode = 0x01000000;
    uint8_t* buff = nullptr;
    std::queue<std::tuple<uint8_t*, size_t, int64_t>> output_h264data;
    size_t outSize = 0;
    int frameIndex = 0;
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

    AclLiteError imgread(const std::string& file, AclImage& data) {
      return imgreadHandle(file, data);
    }

    AclImage imgread(const std::string& file) {
      AclImage img;
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

    AclLiteError initDecodeOutputDesc(const PicDesc& pic, AclImage& inputImage) {
      auto socVersion = aclrtGetSocName();
      if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0) {
        inputImage.width = ALIGN_UP2(pic.width);
        inputImage.height = ALIGN_UP2(pic.height);
        inputImage.widthStride = ALIGN_UP64(pic.width); // 64-byte alignment
        inputImage.heightStride = ALIGN_UP16(pic.height); // 16-byte alignment
      }
      else {
        inputImage.width = pic.width;
        inputImage.height = pic.height;
        inputImage.widthStride = ALIGN_UP128(pic.width); // 128-byte alignment
        inputImage.heightStride = ALIGN_UP16(pic.height); // 16-byte alignment
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

    ~ImageHandler() { close(); }

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

    AclLiteError overlay(const AclImage& src1, ImageData& dest, uint32_t targetX, uint32_t targetY) {
      AclLiteError ret = pasteHandle(src1, dest, targetX, targetY);
      return ret;
    }

    AclLiteError cvtColor(const AclImage& src, AclImage& dest) {
      AclLiteError ret = convertHandle(src, dest);
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
    AclLiteError initCropOrPasteInputDesc(const AclImage& inputImage) {
      uint32_t alignWidth = ALIGN_UP16(inputImage.width);
      uint32_t alignHeight = ALIGN_UP2(inputImage.height);
      if (alignWidth == 0 || alignHeight == 0) {
        E_LOG("Invalid image parameters, width={}, height={}",
          inputImage.width, inputImage.height);
        return ACLLITE_ERROR;
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
      //  return ACLLITE_ERROR;
      //}

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

      //aclError ret = acldvppMalloc(&inputBuffer, inputImage.size);
      //ret = aclrtMemcpy(inputBuffer, inputImage.size, inputImage.data, inputImage.size,
      //  ACL_MEMCPY_DEVICE_TO_DEVICE); 
      //if (ret != ACL_SUCCESS) {
      //  E_LOG("memcpy failed. raw size is {}, Input buffer size is {}, ret={}",
      //    inputImage.size, inputImage.size, ret);
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
        inputImage.width, inputImage.height, alignWidth, alignHeight, inputImage.format, inputImage.size);

      acldvppSetPicDescData(inputPicDesc, inputImage.data);
      acldvppSetPicDescFormat(inputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(inputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(inputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(inputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(inputPicDesc, alignHeight);
      acldvppSetPicDescSize(inputPicDesc, inputImage.size);

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
        inputImage.width, inputImage.height, inputImage.alignWidth, inputImage.alignHeight, inputImage.format, inputImage.size);

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        ACLLITE_LOG_ERROR("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
      }
      acldvppSetPicDescData(outputPicDesc, inputImage.data.get());
      acldvppSetPicDescFormat(outputPicDesc, inputImage.format);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, inputImage.alignWidth);
      acldvppSetPicDescHeightStride(outputPicDesc, inputImage.alignHeight);
      acldvppSetPicDescSize(outputPicDesc, inputImage.size);

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

    AclLiteError pasteHandle(const AclImage& topImg, ImageData& destImg, uint32_t targetX, uint32_t targetY) {
      //初始化输入图片信息描述
      if (initCropOrPasteInputDesc(topImg) != ACLLITE_OK) {
        return ACLLITE_ERROR;
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

      // 必须为奇数
      uint32_t pasteRightOffset = pasteLeftOffset + topImg.width;
      pasteRightOffset = pasteRightOffset - ((pasteRightOffset & 1) ^ 1);
      uint32_t pasteBottomOffset = pasteTopOffset + topImg.height;
      pasteBottomOffset = pasteBottomOffset - ((pasteBottomOffset & 1) ^ 1);
      
      pasteArea_ = acldvppCreateRoiConfig(pasteLeftOffset, pasteRightOffset,
        pasteTopOffset, pasteBottomOffset);
      if (pasteArea_ == nullptr) {
        E_LOG("acldvppCreateRoiConfig pasteArea_ failed");
        return ACLLITE_ERROR;
      }

      aclError aclRet = acldvppVpcCropAndPasteAsync(channelDesc, inputPicDesc,
        outputPicDesc, cropArea_, pasteArea_, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::pasteHandle] acldvppVpcCropAndPasteAsync failed, aclRet={}", aclRet);
        return ACLLITE_ERROR;
      }

      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::pasteHandle] use aclrtSynchronizeStream failed, aclRet={}", aclRet);
        return ACLLITE_ERROR;
      }

      destroyCropOrPasteResource();
      
      return ACLLITE_OK;
    }

    //格式转换输入图片描述初始化
    AclLiteError initConvertpicDesc(const AclImage& inputImage, AclImage& outputImage) {
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
      else if (inputImage.format == PIXEL_FORMAT_YUV_SEMIPLANAR_444) {
        inputBufferSize = YUV444SP_SIZE(alignWidth, alignHeight);
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

      inputPicDesc = acldvppCreatePicDesc();
      if (inputPicDesc == nullptr) {
        E_LOG("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
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
        return ACLLITE_ERROR;
      }
      outputImage.width = inputImage.width;
      outputImage.height = inputImage.height;
      outputImage.widthStride = alignWidth;
      outputImage.heightStride = alignHeight;
      outputImage.size = inputBufferSize;
      outputImage.format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

      outputPicDesc = acldvppCreatePicDesc();
      if (outputPicDesc == nullptr) {
        ACLLITE_LOG_ERROR("Dvpp crop create pic desc failed");
        return ACLLITE_ERROR;
      }
      acldvppSetPicDescData(outputPicDesc, outputImage.data);
      acldvppSetPicDescFormat(outputPicDesc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
      acldvppSetPicDescWidth(outputPicDesc, inputImage.width);
      acldvppSetPicDescHeight(outputPicDesc, inputImage.height);
      acldvppSetPicDescWidthStride(outputPicDesc, alignWidth);
      acldvppSetPicDescHeightStride(outputPicDesc, alignHeight);
      acldvppSetPicDescSize(outputPicDesc, outputBufferSize);
      return ACLLITE_OK;
    }

    AclLiteError convertHandle(const AclImage& src, AclImage& dest) {
      if (initConvertpicDesc(src, dest) != ACLLITE_OK) {
        E_LOG("[ImageHandler::convertHandle] init input and output picdesc failed");
        return ACLLITE_ERROR;
      }

      aclError aclRet = acldvppVpcConvertColorAsync(channelDesc, inputPicDesc, outputPicDesc, stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::convertHandle] acldvppVpcConvertColorAsync failed, aclRet={}", aclRet);
        return ACLLITE_ERROR;
      }
      aclRet = aclrtSynchronizeStream(stream_);
      if (aclRet != ACL_SUCCESS) {
        E_LOG("[ImageHandler::convertHandle] aclrtSynchronizeStream failed, aclRet={}", aclRet);
        return ACLLITE_ERROR;
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