/**
@project VideoEngine23
@author lmx zpx
@since 2023/2/27

*/

#pragma once
#include "seeker/common.h"
#include "seeker/loggerApi.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include "libavutil/avutil.h"
}

#include <string>
#include <vector>
#include <mutex>
#include <queue>

namespace VideoEngine23 {

#define INBUF_SIZE 4096
#define BUF_SIZE 1024 * 1024
#define MTU 1300
  class Decoder23 {
  private:
    AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = NULL;
    AVFrame* gpuFrame = nullptr;  //transfer前在显存中的frame
    AVFormatContext* fmt_ctx = nullptr;
    bool useGpu = 0;
    bool transfer = false;
    int frameNum = 0;
    int ret = 0;
    bool decodeReady = false;
    int video_stream_index;
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_CUDA;

  public:
    Decoder23() {}

    ~Decoder23() {
      close();
    }

    void open(AVDictionary* options = nullptr) {
      D_LOG("Decoder23 open begin");
      if (!useGpu && codec_ctx == nullptr) {
        codec = avcodec_find_decoder_by_name("h264");
        if (!codec) {
          throw std::runtime_error("Codec not found");
        }
        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
          throw std::runtime_error("could not allocate video codec context");
        }
      }

      //设置avcodec_receive_packet缓冲区不必填满
      if (options == nullptr) {
        W_LOG("Decode AVDictionary options is not set");
        av_dict_set(&options, "tune", "zerolatency", 0);
      }
      if (avcodec_open2(codec_ctx, codec, &options) < 0) {
        throw std::runtime_error("video could not open codec");
      }
      av_dict_free(&options);

      decodeReady = true;
      D_LOG("Decoder23 open finish");
    }

    int getFrame(const AVPacket* packet, AVFrame& frame) {
      if (!transfer) {
        frameNum++;
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
          E_LOG("Error sending a packet for decoding");
          return 2;
        }
        ret = avcodec_receive_frame(codec_ctx, &frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          //E_LOG("Error decoding");
          return 1;
        }
        else if (ret < 0) {
          E_LOG("Error during decoding");
          return 1;
        }
      }
      else {
        if (gpuFrame == nullptr) {
          gpuFrame = av_frame_alloc();
        }
        av_frame_unref(gpuFrame);
        frameNum++;
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
          E_LOG("Error sending a packet for decoding");
          return 2;
        }
        ret = avcodec_receive_frame(codec_ctx, gpuFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          return 1;
        else if (ret < 0) {
          E_LOG("Error during decoding");
          return 1;
        }
        //把显存的frame转成内存的frame
        if (gpuFrame->format == AV_PIX_FMT_CUDA) {
          av_hwframe_transfer_data(&frame, gpuFrame, 0);
          D_LOG("frame1->format:{}", frame.format);
          av_frame_unref(gpuFrame);
        }
      }
      return 0;
    }

    AVCodecContext* getContext() {
      if (!useGpu && codec_ctx == nullptr) {
        codec = avcodec_find_decoder_by_name("h264");
        if (!codec) {
          throw std::runtime_error("Codec not found");
        }
        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
          throw std::runtime_error("could not allocate video codec context");
        }
      }
      return codec_ctx;
    }

    void enableHwDevice(AVBufferRef* hwDeviceContext, AVPixelFormat fmt, bool trans) {
      codec = avcodec_find_decoder_by_name("h264_cuvid");
      if (!codec) {
        E_LOG("Codec not found");
      }
      codec_ctx = avcodec_alloc_context3(codec);
      codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
      if (!codec_ctx) {
        throw std::runtime_error("could not allocate video codec context");
      }
      useGpu = true;
      transfer = trans;
      codec_ctx->hw_device_ctx = av_buffer_ref(hwDeviceContext);
      codec_ctx->pix_fmt = fmt;
    }

    int getPacket(const char* filename, AVPacket& inPacket, AVBufferRef* hw_device_ctx = nullptr, bool useGpu = false) {
      int err;
      if (fmt_ctx == nullptr) {
        fmt_ctx = avformat_alloc_context();
        avcodec_free_context(&codec_ctx);
        if ((err = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
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

        if (useGpu) {
          for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) {
              E_LOG("Decoder {} does not support device type {}.",
                codec->name, av_hwdevice_get_type_name(type));
              return -1;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
              config->device_type == type) break;
          }
        }

        /* create decoding context */
        if (!(codec_ctx = avcodec_alloc_context3(codec))) {
          close();
          return AVERROR(ENOMEM);
        }

        if (avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar) < 0) {
          throw std::runtime_error("ERROR: avcodec_parameters_to_context error, add codec param to codec failed.");
        }

        if (useGpu) {
          codec_ctx->pix_fmt = AV_PIX_FMT_CUDA;
          codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        }

        /* init the video decoder */
        if ((err = avcodec_open2(codec_ctx, codec, NULL)) < 0) {
          throw std::runtime_error("ERROR: avcodec_open2 error, open video decoder failed.");
          close();
          return err;
        }
      }
      else {
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
        if (inPacket.stream_index == video_stream_index) {
          return 0;
        }
        else return 2;//inPacket.stream_index不为视频流
      }
      return 0;
    }

    void close() {
      if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
      }
      if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
      }
      if (gpuFrame) {
        av_frame_free(&gpuFrame);
        gpuFrame = nullptr;
      }

      decodeReady = false;
    }
  };
  class Encoder23 {
  private:
    bool useGPU = false;
    bool transFer = false;
    AVCodecContext* encodec_ctx = nullptr;
    AVCodec* encodec = nullptr;
    AVPixelFormat format = AV_PIX_FMT_NONE;
    int current_frame_index = 1;
    AVBufferRef* hw_frames_ref = nullptr;
    AVFrame* GPUframe = nullptr;
    AVDictionary* dict = nullptr;
    AVDictionaryEntry* pEntry = nullptr;

  public:
    Encoder23() {}

    ~Encoder23() { close(); }

    void enableHwDevice(AVBufferRef* hwDeviceContext, AVPixelFormat fmt, bool transfer) {
      if (!(hw_frames_ref = av_hwframe_ctx_alloc(hwDeviceContext))) {
        throw std::runtime_error("Failed to create hw frame context");
      }
      if (fmt == AV_PIX_FMT_CUDA) fmt = AV_PIX_FMT_YUV420P;
      format = fmt;
      useGPU = true;
      transFer = transfer;
    }

    AVCodecContext* getContext() {
      if (encodec_ctx) return encodec_ctx;
      if (useGPU)
        encodec = avcodec_find_encoder_by_name("h264_nvenc");
      else
        encodec = avcodec_find_encoder_by_name("libx264");
      if (!encodec) {
        throw std::runtime_error("Error! find encode failed");
      }
      encodec_ctx = avcodec_alloc_context3(encodec);
      if (!encodec_ctx) {
        throw std::runtime_error("Error! create encode context failed");
      }
      return encodec_ctx;
    }

    void rateControlPreset(const int& maxRate) {
      av_dict_set(&dict, "preset", "p1", 0);
      av_dict_set(&dict, "tune", "ull", 0);
      av_dict_set_int(&dict, "rc", 2, 0);
      av_dict_set(&dict, "profile", "baseline", 0);
      av_dict_set_int(&dict, "level", 31, 0);
      av_dict_set_int(&dict, "cbr", 1, 0);
      av_dict_set_int(&dict, "forced-idr", 1, 0);
      av_dict_set_int(&dict, "zerolatency", 1, 0);
      W_LOG("User is using encoder preset.");
      while ((pEntry = av_dict_get(dict, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
        I_LOG("metadata : {}={}\n", pEntry->key, pEntry->value);
      }
      auto ctx = getContext();
      ctx->rc_max_rate = maxRate;
      ctx->bit_rate = ctx->rc_max_rate - 300000;
      int quantify = ctx->bit_rate / 1000000;
      ctx->rc_buffer_size = ctx->bit_rate * 0.5 * (1 - (quantify * 0.2));
      ctx = nullptr;
    }

    void open(AVDictionary* options = nullptr) {
      encodec_ctx->color_range = AVCOL_RANGE_MPEG;
      encodec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
      encodec_ctx->codec_id = encodec->id;
      encodec_ctx->thread_count = 1;
      encodec_ctx->slice_count = 1;
      encodec_ctx->slices = 1;
      if (useGPU) {//GPU模式                 
        AVHWFramesContext* frames_ctx = nullptr;
        int err = 0;
        frames_ctx = (AVHWFramesContext*)(hw_frames_ref->data);
        frames_ctx->format = AV_PIX_FMT_CUDA;
        frames_ctx->sw_format = format;
        frames_ctx->width = encodec_ctx->width;
        frames_ctx->height = encodec_ctx->height;
        if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
          av_buffer_unref(&hw_frames_ref);
          throw std::runtime_error("av_hwframe_ctx_init failed");
        }
        encodec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
        if (!encodec_ctx->hw_frames_ctx)
          err = AVERROR(ENOMEM);
        av_buffer_unref(&hw_frames_ref);
        if (options == nullptr) {
          if (dict == nullptr) {
            W_LOG("Encode will automatic settings AVDictionary dicts");
            av_dict_set_int(&dict, "delay", 0, 0);
            av_dict_set_int(&dict, "forced-idr", 1, 0);
            av_dict_set(&dict, "profile", "baseline", 0);
            while ((pEntry = av_dict_get(dict, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
              I_LOG("metadata : {}={}\n", pEntry->key, pEntry->value);
            }
          }
          if (avcodec_open2(encodec_ctx, encodec, &dict) < 0) {
            throw std::runtime_error("open encode context failed");
          }
          av_dict_free(&dict);
          dict = nullptr;
        }
        else {
          while ((pEntry = av_dict_get(options, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
            I_LOG("metadata : {}={}\n", pEntry->key, pEntry->value);
          }
          if (avcodec_open2(encodec_ctx, encodec, &options) < 0) {
            throw std::runtime_error("open encode context failed");
          }
        }
      }
      else {//CPU模式
        if (options == nullptr) {
          if (dict == nullptr) {
            W_LOG("Encode will automatic settings AVDictionary dicts");
            av_dict_set(&dict, "tune", "zerolatency", 0);
            av_dict_set(&dict, "profile", "baseline", 0);
            while ((pEntry = av_dict_get(dict, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
              I_LOG("metadata : {}={}\n", pEntry->key, pEntry->value);
            }
          }
          if (avcodec_open2(encodec_ctx, encodec, &dict) < 0) {
            throw std::runtime_error("open encode context failed");
          }
          av_dict_free(&dict);
          dict = nullptr;
        }
        else {
          while ((pEntry = av_dict_get(options, "", pEntry, AV_DICT_IGNORE_SUFFIX))) {
            I_LOG("metadata : {}={}\n", pEntry->key, pEntry->value);
          }
          if (avcodec_open2(encodec_ctx, encodec, &options) < 0) {
            throw std::runtime_error("open encode context failed");
          }
        }
      }
    }

    int getPacket(AVFrame* srcFrame, AVPacket& packet) {
      int err = 0;
      if (encodec_ctx->width != srcFrame->width || encodec_ctx->height != srcFrame->height) {
        throw std::runtime_error("ERROR: please reset Encoder size.");
        return -1;
      }
      if (transFer) {
        if (GPUframe == nullptr) {
          GPUframe = av_frame_alloc();
        }
        av_frame_unref(GPUframe);
        err = av_hwframe_get_buffer(encodec_ctx->hw_frames_ctx, GPUframe, 0);
        if (err != 0) {
          throw std::runtime_error("ERROR: av_hwframe_get_buffer error, unkonwn failed.");
          return -1;
        }
        if (!GPUframe->hw_frames_ctx) {
          err = AVERROR(ENOMEM);
          return -2;
        }
        av_hwframe_transfer_data(GPUframe, srcFrame, 0);
        T_LOG("TRACE: after transfer, frame_ pix format:{}", GPUframe->format);
        err = avcodec_send_frame(encodec_ctx, GPUframe);
        if (err < 0) {
          if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
            W_LOG("WARNING: send frame failed, errCode={}", err);
          }
          else {
            E_LOG("ERROR: send frame failed, errCode={}", err);
          }
        }
        av_frame_unref(GPUframe);
      }
      else {
        err = avcodec_send_frame(encodec_ctx, srcFrame);
        if (err < 0) {
          if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
            W_LOG("WARNING: send frame failed, errCode={}", err);
          }
          else {
            E_LOG("ERROR: send frame failed, errCode={}", err);
          }
        }
      }
      err = avcodec_receive_packet(encodec_ctx, &packet);
      if (err < 0) {
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
          D_LOG("WARNING: receive AVPacket failed, errCode={}", err);
        }
        else {
          E_LOG("ERROR: receive AVPacket failed, errCode={}", err);
          av_packet_unref(&packet);
          return -1;
        }
      }
      return 0;
    }

    void close() {
      if (encodec_ctx) {
        avcodec_free_context(&encodec_ctx); //°üº¬AVCodecµÄÊÍ·Å£¨avcodec_close())
        encodec_ctx = nullptr;
      }
      if (GPUframe) {
        av_frame_free(&GPUframe);
        GPUframe = nullptr;
      }
    }

  };

  class Parser23 {
  private:
    AVCodecParserContext* parser = NULL;
    AVCodecContext* codec_ctx = NULL;
    int p_loadData = 0;   //rtp payLoad读的位置
    int p_buff = 0;   //h264data写的位置
    int dataLen = 0;
    unsigned int h264_startcode = 0x01000000;
    uint8_t* buff = nullptr;
    std::queue<std::tuple<uint8_t*, size_t, int64_t>> output_h264data;
    size_t outSize = 0;
    AVPacket* tmpPkt = nullptr;//通道维护并使用的AVPacket对象，用于引用由nalu片拼接好的完整pkt数据
    int64_t frame_ts = -1;
    int frameIndex = 0;
  public:
    Parser23(AVCodecContext* ccodec_ctx) {
      if (ccodec_ctx == nullptr) {
        throw std::runtime_error("AVCodecContext is nullptr");
      }
      codec_ctx = ccodec_ctx;
      buff = new uint8_t[BUF_SIZE];
      parser = av_parser_init(AV_CODEC_ID_H264);
      D_LOG("parser init!");
      if (!parser) {
        throw std::runtime_error("parser not found");
      }
    }

    ~Parser23() {
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
    }

    int demux(const uint8_t* data, int len, int64_t timestamp, AVPacket* packet) {
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
        int ret = av_parser_parse2(parser, codec_ctx, &tmpPkt->data, &tmpPkt->size,
          h264Data, inputLen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
          E_LOG("parser wrong");
        }
        count += ret;
        inputLen -= ret;
        tmpPkt->pts = ts;
        if (tmpPkt->size) {
          num++;
          av_packet_ref(packet, tmpPkt);
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
  private:
    void inputPayload(const uint8_t* loadData, int len, int64_t timestamp) {
      D_LOG("timestamp:{}", timestamp);
      uint8_t load_hdr = loadData[0];
      uint8_t fu_ind = loadData[0];   //分片
      uint8_t fu_hdr = loadData[1];   //分片
      D_LOG("fu_hdr:{}", fu_hdr);

      D_LOG("load_hdr:{}", load_hdr);
      //聚合
      if ((load_hdr & 0x1f) == 24) {
        p_buff = 0;
        dataLen = len - 1;//去掉load_hdr
        p_loadData = 1;

        while (dataLen > 0) {
          memcpy(&buff[p_buff], &h264_startcode, 4);
          p_buff += 4;
          uint16_t size = (loadData[p_loadData] << 8) | loadData[p_loadData + 1];
          p_loadData += 2;
          dataLen -= 2;
          memcpy(&buff[p_buff], &loadData[p_loadData], (size_t)size);
          p_loadData += size;
          p_buff += size;
          dataLen -= size;
        }
        outSize = p_buff;
        output_h264data.push(std::make_tuple(buff, outSize, timestamp));
      }

      //单个
      else if ((load_hdr & 0x1f) > 0 && (load_hdr & 0x1f) < 24) {
        p_loadData = 0;
        p_buff = 0;
        memcpy(&buff[p_buff], &h264_startcode, 4);
        p_buff += 4;
        memcpy(&buff[p_buff], &loadData[p_loadData], len);
        p_buff += len;
        outSize = p_buff;
        output_h264data.push(std::make_tuple(buff, p_buff, timestamp));
      }

      //分片
      else if (((load_hdr & 0x1f) == 28) && len >= 2) {

        dataLen = len - 2;
        p_loadData = 2;

        //S=1,nalu的开始
        if (fu_hdr >> 7 == 1) {
          uint8_t F = fu_ind & 0x80;
          uint8_t NRI = fu_ind & 0x60;
          uint8_t Type = fu_hdr & 0x1f;
          uint8_t nalu_hdr = F | NRI | Type;
          p_buff = 0;
          memcpy(&buff[p_buff], &h264_startcode, 4);
          p_buff += 4;
          memcpy(&buff[p_buff], &nalu_hdr, 1);
          p_buff += 1;
          memcpy(&buff[p_buff], &loadData[p_loadData], dataLen);
          p_buff += dataLen;
        }

        //E=1,nalu的结束
        else if ((fu_hdr >> 6) == 1) {
          memcpy(&buff[p_buff], &loadData[p_loadData], dataLen);
          p_buff += dataLen;
          outSize = p_buff;
          output_h264data.push(std::make_tuple(buff, outSize, timestamp));
        }

        //S=E=0，中间
        else if ((fu_hdr >> 5) == 0) {
          memcpy(&buff[p_buff], &loadData[p_loadData], dataLen);
          p_buff += dataLen;
        }
      }
      D_LOG("output_h264data.size():{}", output_h264data.size());
    }

    int getH264(uint8_t* h264Data, int64_t& timestamp) {
      if (output_h264data.empty())
      {
        return 0;
      }
      else {
        std::tuple<uint8_t*, size_t, int64_t> firstData = output_h264data.front();
        memcpy(h264Data, std::get<0>(firstData), std::get<1>(firstData));
        timestamp = std::get<2>(firstData);
        output_h264data.pop();
        return std::get<1>(firstData);
      }
    }
  };

  class Mux23 {
  public:
    Mux23() {}

    ~Mux23() {}

    int mux(const AVPacket* packet, std::queue<std::vector<uint8_t>>& inData) {
      if (packet != nullptr && packet->size) {
        H264ToRtp(packet, inData);
      }
      else {
        E_LOG("ERROR: encode pkt is nullptr.");
        return -1;
      }
      return 0;
    }

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
    void H264ToRtp(const AVPacket* input_pkt, std::queue<std::vector<uint8_t>>& outData) {
      uint64_t frameCount = 0;
      auto data = input_pkt->data;
      auto pktSize = input_pkt->size;
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

  class Tools23 {

  public:

    void saveFileH264(AVPacket& inPacket, FILE* fileStream) {
      fwrite(inPacket.data, 1, inPacket.size, fileStream);
    }

    void saveFileYUV(AVFrame& inFrame, FILE* fileStream) {
      //int size = inFrame.width * inFrame.height;
      //fwrite(inFrame.data[0], 1, size, fileStream);
      //fwrite(inFrame.data[1], 1, size / 4, fileStream);
      //fwrite(inFrame.data[2], 1, size / 4, fileStream);

      int picSize = inFrame.height * inFrame.width;
      int newSize = picSize * 1.5;
      unsigned char* buf = new unsigned char[newSize];
      int a = 0, i;
      for (i = 0; i < inFrame.height; i++)
      {
        memcpy(buf + a, inFrame.data[0] + i * inFrame.linesize[0], inFrame.width);
        a += inFrame.width;
      }
      for (i = 0; i < inFrame.height / 2; i++)
      {
        memcpy(buf + a, inFrame.data[1] + i * inFrame.linesize[1], inFrame.width / 2);
        a += inFrame.width / 2;
      }
      for (i = 0; i < inFrame.height / 2; i++)
      {
        memcpy(buf + a, inFrame.data[2] + i * inFrame.linesize[2], inFrame.width / 2);
        a += inFrame.width / 2;
      }
      fwrite(buf, 1, newSize, fileStream);
      delete buf;
      buf = nullptr;

    }

    void scale(AVFrame* frame, int width, int height, AVPixelFormat fmt = AV_PIX_FMT_NONE) {

      int swsPixFmt = fmt == AV_PIX_FMT_NONE ? frame->format : fmt;
      AVFrame* swsFrame = nullptr;
      swsFrame = av_frame_alloc();
      swsFrame->width = width;
      swsFrame->height = height;
      swsFrame->format = swsPixFmt;
      av_frame_get_buffer(swsFrame, 0);

      struct SwsContext* swsContext = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), width, height, AVPixelFormat(swsPixFmt), SWS_BILINEAR, NULL, NULL, NULL);

      int aaa = sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, swsFrame->data, swsFrame->linesize);

      av_frame_unref(frame);
      int re = av_frame_ref(frame, swsFrame);

      av_frame_unref(swsFrame);
      av_frame_free(&swsFrame);
      sws_freeContext(swsContext);
    }
  };

}