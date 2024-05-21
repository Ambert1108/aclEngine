// @brief: aclengine数据类型定义
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.5.20]
// @version: V0.0.1
// @revision: [xxx@2024.5.20]

#pragma once

#include <unistd.h>
#include <string>
#include <memory>

#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"

namespace acle {
  struct CodecFormat {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t maxBitrate = 960;

    /* 关键帧间隔 <Ambert May-20-2024>*/
    uint32_t gopSize = 12;

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

    ~AclPacket() {
      //if(data) delete[] ((uint8_t*)data);
    };
  };
}