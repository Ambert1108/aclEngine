// @brief: aclengine数据类型定义
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.5.20]
// @version: V0.0.1
// @revision: [xxx@2024.5.20]

#pragma once

#include <unistd.h>
#include <string>
#include <memory>

#include"seeker/logger.h"
#include "seeker/loggerApi.h"
#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"

namespace acle {
  struct CodecFormat {
    uint32_t width = 0;
    uint32_t height = 0;
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
    acldvppStreamFormat enType = H264_MAIN_LEVEL;
    aclrtContext context = nullptr;
    aclrtRunMode runMode = ACL_HOST;
    std::string outFile;
  };

  struct PicDesc {
    std::string picName;
    uint32_t width;
    uint32_t height;
    acldvppJpegFormat format;
    uint32_t jpegDecodeSize;
  };

  struct AclImage {
    acldvppPixelFormat format;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t widthStride = 0;
    uint32_t heightStride = 0;
    void* data = nullptr;
    uint32_t size = 0;

    AclImage() {
      I_LOG("AclImage()");
    };

    AclImage(const AclImage& image) {
      format = image.format;
      width = image.width;
      height = image.height;
      widthStride = image.widthStride;
      heightStride = image.heightStride;
      size = image.size;
      acldvppMalloc(&data, size);
      aclrtMemcpy(data, size, image.data, image.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
      I_LOG("AclImage(const AclImage& image) noexcept");
    };

    AclImage(AclImage&& image) noexcept :
      format(image.format),
      width(image.width),
      height(image.height),
      widthStride(image.widthStride),
      heightStride(image.heightStride),
      size(image.size),
      data(std::move(image.data)) { };

    ~AclImage() {
      if (data) {
        acldvppFree(data);
        data = nullptr;
      }
    };

    AclImage& operator=(const AclImage& image) {
      this->format = image.format;
      this->width = image.width;
      this->height = image.height;
      this->widthStride = image.widthStride;
      this->heightStride = image.heightStride;
      this->size = image.size;
      acldvppMalloc(&this->data, this->size);
      aclrtMemcpy(this->data, this->size, image.data, image.size, ACL_MEMCPY_DEVICE_TO_DEVICE);
      I_LOG("AclImage& operator=(const AclImage& image)");
      return *this;
    }
  };


  struct AclFrame {
    acldvppPixelFormat format;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t alignWidth = 0;
    uint32_t alignHeight = 0;
    uint32_t size = 0;
    std::shared_ptr<uint8_t> data = nullptr;
  };

  struct AclPacket {
    uint8_t* data;
    uint32_t size;
    uint64_t timestamp;

    /* 1:true, 0:false */
    uint8_t eos;

    AclPacket() = default;

    //AclPacket& operator=(const AclPacket& pkt) {
    //  size = pkt.size;
    //  timestamp = pkt.timestamp;
    //  acldvppMalloc((void**)(&data), size);
    //  aclrtMemcpy((void*))
    //}

    ~AclPacket() {
      if (data) {
        acldvppFree(data);
        data = nullptr;
      }
    };
  };

/**
* @brief calculate RGB 24bits image size
* @param [in]: width:  image width
* @param [in]: height: image height
* @return bytes size of image
*/
#define RGBU8_IMAGE_SIZE(width, height) ((width) * (height) * 3)

/**
* @brief calculate RGB C3F32 image size
* @param [in]: width:  image width
* @param [in]: height: image height
* @return bytes size of image
*/
#define RGBF32_IMAGE_SIZE(width, height) ((width) * (height) * 3 * sizeof(float))

/**
* @brief calculate YUVSP420 image size
* @param [in]: width:  image width
* @param [in]: height: image height
* @return bytes size of image
*/
#define YUV420SP_SIZE(width, height) ((width) * (height) * 3 / 2)

/**
* @brief calculate YUVSP444 image size
* @param [in]: width:  image width
* @param [in]: height: image height
* @return bytes size of image
*/
#define YUV444SP_SIZE(width, height) ((width) * (height) * 3)

/**
* @brief calculate YUVSP420 nv12 load to opencv mat height paramter
* @param [in]: height: yuv image height
* @return bytes size of image
*/
#define YUV420SP_CV_MAT_HEIGHT(height) ((height) * 3 / 2)

/**
* @brief generate shared pointer of dvpp memory
* @param [in]: buf: memory pointer, malloc by acldvppMalloc
* @return shared pointer of input buffer
*/
#define SHARED_PTR_DVPP_BUF(buf) (std::shared_ptr<uint8_t>((uint8_t *)(buf), [](uint8_t* p) { acldvppFree(p); }))

/**
* @brief generate shared pointer of device memory
* @param [in]: buf: memory pointer, malloc by acldvppMalloc
* @return shared pointer of input buffer
*/
#define SHARED_PTR_DEV_BUF(buf) (std::shared_ptr<uint8_t>((uint8_t *)(buf), [](uint8_t* p) { aclrtFree(p); }))


  struct Resolution {
    uint32_t width = 0;
    uint32_t height = 0;
  };
}