// @brief: 操作工具接口
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.6.17]
// @version: V0.0.1
// @revision: [xxx@2024.6.17]

#pragma once

#include <unistd.h>
#include <memory>

#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"
#include "acl/acl_rt.h"

namespace acle{
  enum CopyDirection {
    TO_DEVICE = 0,
    TO_HOST,
    INVALID_COPY_DIRECT
  };

  enum MemoryType {
    NORMAL = 0,
    HOST,
    DEVICE,
    DVPP,
    INVALID_TYPE
  };

  inline aclrtMemcpyKind getCopyPolicy(aclrtRunMode srcDev, CopyDirection direct, MemoryType memType) {
    aclrtMemcpyKind policy = ACL_MEMCPY_HOST_TO_HOST;

    if (direct == TO_DEVICE) {
      if (srcDev == ACL_HOST)
        policy = ACL_MEMCPY_HOST_TO_DEVICE;
      else
        policy = ACL_MEMCPY_DEVICE_TO_DEVICE;
    }
    else {
      if (srcDev == ACL_DEVICE)
        policy = ACL_MEMCPY_DEVICE_TO_HOST;
    }

    return policy;
  }

  inline void* mallocMemory(uint32_t dataSize, MemoryType memType) {
    void* buffer = nullptr;
    aclError aclRet = ACL_SUCCESS;
    switch (memType) {
    case NORMAL:
      buffer = new uint8_t[dataSize];
      break;
    case HOST:
      aclRet = aclrtMallocHost(&buffer, dataSize);
      break;
    case DEVICE:
      aclRet = aclrtMalloc(&buffer, dataSize, ACL_MEM_MALLOC_HUGE_FIRST);
      break;
    case DVPP:
      aclRet = acldvppMalloc(&buffer, dataSize);
      break;
    default:
      E_LOG("Invalid memory type {}", memType);
      aclRet = ACL_ERROR_INVALID_PARAM;
      break;
    }

    if ((aclRet != ACL_SUCCESS) || (buffer == nullptr)) {
      E_LOG("Malloc memory failed, type:{}, error:{}",
        memType, aclRet);
      return nullptr;
    }
    return buffer;
  }

  inline void freeMemory(void* mem, MemoryType memType) {
    switch (memType) {
    case NORMAL:
      delete[]((uint8_t*)mem);
      break;
    case HOST:
      aclrtFreeHost(mem);
      break;
    case DEVICE:
      aclrtFree(mem);
      break;
    case DVPP:
      acldvppFree(mem);
      break;
    default:
      E_LOG("Invalid memory type %d", memType);
      break;
    }
  }

  inline void* copyData(const void* data, uint32_t size, aclrtMemcpyKind policy, MemoryType memType) {
    void* buffer = mallocMemory(size, memType);
    if (buffer == nullptr) {
      return nullptr;
    }

    aclError aclRet = aclrtMemcpy(buffer, size, data, size, policy);
    if (aclRet != ACL_SUCCESS) {
      E_LOG("Copy data to device failed, ret is {}", aclRet);
      freeMemory(buffer, memType);
      return nullptr;
    }

    return buffer;
  }

  inline void* copyDataToDevice(const void* data, uint32_t size,
    aclrtRunMode curRunMode, MemoryType memType) {
    if ((data == nullptr) || (size == 0) ||
      ((curRunMode != ACL_HOST) && (curRunMode != ACL_DEVICE)) ||
      (memType >= INVALID_TYPE) || (memType == HOST)) {
      E_LOG("Copy data args invalid, data {}, "
        "size {}, src dev {}, memory type {}",
        data, size, curRunMode, memType);
      return nullptr;
    }

    aclrtMemcpyKind policy = getCopyPolicy(curRunMode, TO_DEVICE, memType);

    return copyData(data, size, policy, memType);
  }

  inline void* copyDataToHost(const void* data, uint32_t size,
    aclrtRunMode curRunMode, MemoryType memType) {
    if ((data == nullptr) || (size == 0) ||
      ((curRunMode != ACL_HOST) && (curRunMode != ACL_DEVICE)) ||
      ((memType != HOST) && (memType != NORMAL))) {
      E_LOG("Copy data args invalid, data {}, "
        "size {}, src dev {}, memory type {}",
        data, size, curRunMode, memType);
      return nullptr;
    }

    aclrtMemcpyKind policy = getCopyPolicy(curRunMode, TO_HOST, memType);

    return copyData(data, size, policy, memType);
  }
}