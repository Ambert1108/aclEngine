// @brief: 昇腾操作工具集合
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.6.17]
// @version: V0.0.1
// @revision: [Ambert@2024.6.17]
#pragma once

#include <unistd.h>
#include <memory>
#include "types.h"

#include "seeker/logger.h"
#include "seeker/loggerApi.h"

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
    INVALID
  };

  inline aclrtMemcpyKind getCopyPolicy(aclrtRunMode srcDev, CopyDirection direct, MemoryType memType);
  
  inline void* mallocMemory(uint32_t dataSize, MemoryType memType);
  
  inline void freeMemory(void* mem, MemoryType memType);
  
  inline void* copyData(const void* data, uint32_t size, aclrtMemcpyKind policy, MemoryType memType);

  inline void* copyDataToDevice(const void* data, uint32_t size, aclrtRunMode curRunMode, MemoryType memType);

  inline void* copyDataToHost(const void* data, uint32_t size, aclrtRunMode curRunMode, MemoryType memType);

  struct acl {
    static void setDevice(int32_t id);

  private:
    static int32_t deviceId;
    
    friend class GpuMat;
    friend class Overlay;
    friend class Transfer;
  };
}