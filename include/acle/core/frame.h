// @brief: 昇腾视频帧数据结构封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024. 7.24]
// @version: V0.0.1
// @revision: [Ambert@2024.7.24]

#pragma once
#include "utils.h"

namespace acle {
  struct CodecFormat {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    uint32_t maxBitrate = 2000;

    /* 关键帧间隔 <Ambert May-20-2024>*/
    uint32_t gopSize = 60;

    /*
    * 码率控制模式：默认为0，使用CBR
    * 1：VBR
    * 2：CBR
    * <Ambert May-20-2024>
    */
    uint32_t rcMode = 2;

    /*
    * 输入图像格式，支持如下格式：
    * PIXEL_FORMAT_YUV_SEMIPLANAR_420
    * PIXEL_FORMAT_YVU_SEMIPLANAR_420
    * <Ambert May-20-2024>
    */
    acldvppPixelFormat format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

    /*
    * 编码协议，支持如下格式：
    * H264_BASELINE_LEVEL,
    * H264_MAIN_LEVEL,
    * H264_HIGH_LEVEL
    * <Ambert May-20-2024>
    */
    acldvppStreamFormat enType = H264_BASELINE_LEVEL;
    aclrtContext context = nullptr;
    aclrtRunMode runMode = ACL_HOST;
    std::string file;
  };

  struct PicDesc {
    std::string picName;
    uint32_t width;
    uint32_t height;
    acldvppJpegFormat format;
    uint32_t jpegDecodeSize;
  };

  struct AclFrame {
    acldvppPixelFormat format;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t alignWidth = 0;
    uint32_t alignHeight = 0;
    uint32_t size = 0;
    std::shared_ptr<uint8_t> data = nullptr;
    bool isFinished = false;

    AclFrame() = default;

    AclFrame(const AclFrame& img) {
      this->format = img.format;
      this->width = img.width;
      this->height = img.height;
      this->alignWidth = img.alignWidth;
      this->alignHeight = img.alignHeight;
      this->size = img.size;
      this->data = img.data;
    }

    AclFrame(AclFrame&& img) {
      this->format = std::move(img.format);
      this->width = std::move(img.width);
      this->height = std::move(img.height);
      this->alignWidth = std::move(img.alignWidth);
      this->alignHeight = std::move(img.alignHeight);
      this->size = std::move(img.size);
      this->data = std::move(img.data);
    }

    AclFrame& operator=(const AclFrame& img) {
      this->format = img.format;
      this->width = img.width;
      this->height = img.height;
      this->alignWidth = img.alignWidth;
      this->alignHeight = img.alignHeight;
      this->size = img.size;
      this->data = img.data;
      return *this;
    }
  };

  struct AclPacket {
    std::shared_ptr<uint8_t> data = nullptr;
    int32_t size;
    uint64_t pts;

    /* 1:true, 0:false */
    uint8_t eos;
    MemoryType memType{ INVALID };

    AclPacket() = default;

    AclPacket(const AclPacket& pkt) {
      this->size = pkt.size;
      this->pts = pkt.pts;
      this->eos = pkt.eos;
      this->memType = pkt.memType;
      this->data = pkt.data;
    }

    AclPacket(AclPacket&& pkt) {
      this->size = std::move(pkt.size);
      this->pts = std::move(pkt.pts);
      this->eos = std::move(pkt.eos);
      this->memType = std::move(pkt.memType);
      this->data = std::move(pkt.data);
    }

    AclPacket& operator=(const AclPacket& pkt) {
      this->size = pkt.size;
      this->pts = pkt.pts;
      this->eos = pkt.eos;
      this->memType = pkt.memType;
      this->data = pkt.data;
      return *this;
    }
  };

  struct Resolution {
    uint32_t width = 0;
    uint32_t height = 0;
  };
}