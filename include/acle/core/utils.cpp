#include "utils.h"

namespace acle {
  aclrtMemcpyKind getCopyPolicy(aclrtRunMode srcDev, CopyDirection direct, MemoryType memType) {
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
  };

  void* mallocMemory(uint32_t dataSize, MemoryType memType) {
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
      D_LOG("[AclEngine::mallocMemory] invalid memory type");
      aclRet = ACL_ERROR_INVALID_PARAM;
      break;
    }

    if ((aclRet != ACL_SUCCESS) || (buffer == nullptr)) {
      E_LOG("Malloc memory failed, type:{}, error:{}",
        memType, aclRet);
      return nullptr;
    }
    return buffer;
  };

  void freeMemory(void* mem, MemoryType memType) {
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
      D_LOG("[AclEngine::freeMemory] invalid memory type");
      break;
    }
  };

  void* copyData(const void* data, uint32_t size, aclrtMemcpyKind policy, MemoryType memType) {
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
  };

  void* copyDataToDevice(const void* data, uint32_t size, aclrtRunMode curRunMode, MemoryType memType) {
    if ((data == nullptr) || (size == 0) ||
      ((curRunMode != ACL_HOST) && (curRunMode != ACL_DEVICE)) ||
      (memType >= INVALID) || (memType == HOST)) {
      E_LOG("Copy data args invalid, data {}, "
        "size {}, src dev {}, memory type {}",
        data, size, curRunMode, memType);
      return nullptr;
    }

    aclrtMemcpyKind policy = getCopyPolicy(curRunMode, TO_DEVICE, memType);

    return copyData(data, size, policy, memType);
  };

  void* copyDataToHost(const void* data, uint32_t size, aclrtRunMode curRunMode, MemoryType memType) {
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
  };

  int32_t acl::deviceId = -1;

  void acl::setDevice(int32_t id) { deviceId = id; }
}