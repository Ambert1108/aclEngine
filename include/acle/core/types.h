// @brief: 昇腾数据类型定义
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.5.20]
// @version: V0.0.3
// @revision: [Ambert@2024.7.1]

#pragma once

#include <unistd.h>
#include <string>
#include <fstream>
#include <memory>

#include "seeker/logger.h"
#include "seeker/loggerApi.h"

#define ENABLE_DVPP_INTERFACE
#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"
#include "acl/acl_rt.h"

namespace acle {
  // regex for verify video file name
  const std::string RegexVideoFile = "^.+\\.(mp4|h264|h265)$";

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

/**
* @brief generate shared pointer of normal memory
* @param [in]: buf: memory pointer, malloc by cpp new
* @return shared pointer of input buffer
*/
#define SHARED_PTR_NORMAL_BUF(buf) (std::shared_ptr<uint8_t>((uint8_t *)(buf), [](uint8_t* p) { delete[](p); }))

/**
* @brief calculate aligned number
* @param [in]: num: the original number that to aligned
* @param [in]: align: the align factor
* @return the number after aligned
*/
#define ALIGN_UP(num, align) (((num) + (align) - 1) & ~((align) - 1))

/**
 * @brief calculate number align with 2
 * @param [in]: num: the original number that to aligned
 * @return the number after aligned
 */
#define ALIGN_UP2(num) ALIGN_UP(num, 2)

 /**
  * @brief calculate number align with 16
  * @param [in]: num: the original number that to aligned
  * @return the number after aligned
  */
#define ALIGN_UP16(num) ALIGN_UP(num, 16)

  /**
   * @brief calculate number align with 64
   * @param [in]: num: the original number that to aligned
   * @return the number after aligned
   */
#define ALIGN_UP64(num) ALIGN_UP(num, 64)

   /**
    * @brief calculate number align with 128
    * @param [in]: num: the original number that to aligned
    * @return the number after aligned
    */
#define ALIGN_UP128(num) ALIGN_UP(num, 128)

    /**
     * @brief calculate elements num of array
     * @param [in]: array: the array variable
     * @return elements num of array
     */
#define SIZEOF_ARRAY(array)  (sizeof(array)/sizeof(array[0]))

#define ACLE_8UC1 1
#define ACLE_8UC3 2
#define ACLE_8UC4 3
}