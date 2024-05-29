#include <iostream>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"



#define INFO_LOG(fmt, args...) fprintf(stdout, "[INFO]  " fmt "\n", ##args)
#define WARN_LOG(fmt, args...) fprintf(stdout, "[WARN]  " fmt "\n", ##args)
#define ERROR_LOG(fmt, args...) fprintf(stdout, "[ERROR] " fmt "\n", ##args)

typedef enum Result {
  SUCCESS = 0,
  FAILED = 1
} Result;

typedef struct PicDesc {
  std::string picName;
  uint32_t width;
  uint32_t height;
  uint32_t jpegDecodeSize;
} PicDesc;

int32_t deviceId_ = 0;
aclrtContext context_ = nullptr;
aclrtStream stream_ = nullptr;
acldvppChannelDesc* dvppChannelDesc_;

void* decodeOutDevBuffer_; // decode output buffer
acldvppPicDesc* decodeOutputDesc_; //decode output desc
acldvppPicDesc* encodeDesc_; //encode output desc
uint32_t decodeDataSize_;

void* inDevBuffer_;  // decode input buffer
uint32_t inDevBufferSize_; // dvpp input buffer size
void* outDevBuffer_;  // encode input buffer
uint32_t outDevBufferSize_; // dvpp output buffer size
acldvppJpegeConfig* g_jpegeConfig;

uint32_t inputWidth_; // input pic width
uint32_t inputHeight_; // input pic height
aclrtRunMode runMode;


//decode
void* GetDeviceBufferOfPicture(PicDesc& picDesc, uint32_t& devPicBufferSize) {
  if (picDesc.picName.empty()) {
    ERROR_LOG("picture file name is empty");
    return nullptr;
  }

  FILE* fp = fopen(picDesc.picName.c_str(), "rb");
  if (fp == nullptr) {
    ERROR_LOG("open file %s failed", picDesc.picName.c_str());
    return nullptr;
  }

  fseek(fp, 0, SEEK_END);
  uint32_t fileLen = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  uint32_t inputBuffSize = fileLen;

  char* inputBuff = new(std::nothrow) char[inputBuffSize];
  size_t readSize = fread(inputBuff, sizeof(char), inputBuffSize, fp);
  if (readSize < inputBuffSize) {
    ERROR_LOG("need read file %s %u bytes, but only %zu readed",
      picDesc.picName.c_str(), inputBuffSize, readSize);
    delete[] inputBuff;
    fclose(fp);
    return nullptr;
  }

  void* inBufferDev = nullptr;
  aclError ret = acldvppMalloc(&inBufferDev, inputBuffSize);
  if (ret != ACL_SUCCESS) {
    delete[] inputBuff;
    ERROR_LOG("malloc device data buffer failed, aclRet is %d", ret);
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
    ERROR_LOG("memcpy failed. Input host buffer size is %u",
      inputBuffSize);
    acldvppFree(inBufferDev);
    delete[] inputBuff;
    fclose(fp);
    return nullptr;
  }

  delete[] inputBuff;
  devPicBufferSize = inputBuffSize;
  fclose(fp);
  return inBufferDev;
}

//encode
uint32_t AlignmentHelper(uint32_t origSize, uint32_t alignment) {
  if (alignment == 0) {
    return 0;
  }
  uint32_t alignmentH = alignment - 1;
  return (origSize + alignmentH) / alignment * alignment;
}

uint32_t ComputeEncodeInputSize(int inputWidth, int inputHeight) {
  uint32_t widthAlignment = 16;
  uint32_t heightAlignment = 2;
  uint32_t sizeAlignment = 3;
  uint32_t sizeNum = 2;
  uint32_t encodeInWidthStride = AlignmentHelper(inputWidth, widthAlignment);
  uint32_t encodeInHeightStride = AlignmentHelper(inputHeight, heightAlignment);
  if (encodeInWidthStride == 0 || encodeInHeightStride == 0) {
    ERROR_LOG("ComputeEncodeInputSize AlignmentHelper failed");
    return FAILED;
  }
  uint32_t encodeInBufferSize =
    encodeInWidthStride * encodeInHeightStride * sizeAlignment / sizeNum;
  return encodeInBufferSize;
}

char* GetdevBufferFromPath(const PicDesc& picDesc, uint32_t& PicBufferSize) {
  if (picDesc.picName.empty()) {
    ERROR_LOG("picture file name is empty");
    return nullptr;
  }

  FILE* fp = fopen(picDesc.picName.c_str(), "rb");
  if (fp == nullptr) {
    ERROR_LOG("open file %s failed", picDesc.picName.c_str());
    return nullptr;
  }

  fseek(fp, 0, SEEK_END);
  uint32_t fileLen = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fileLen < PicBufferSize) {
    ERROR_LOG("need read %u bytes but file %s only %u bytes",
      PicBufferSize, picDesc.picName.c_str(), fileLen);
    fclose(fp);
    return nullptr;
  }

  char* inputBuff = new(std::nothrow) char[PicBufferSize];

  size_t readSize = fread(inputBuff, sizeof(char), PicBufferSize, fp);
  if (readSize < PicBufferSize) {
    ERROR_LOG("need read file %s %u bytes, but only %zu readed",
      picDesc.picName.c_str(), PicBufferSize, readSize);
    delete[] inputBuff;
    fclose(fp);
    return nullptr;
  }

  void* inputDevBuff = nullptr;
  aclError aclRet = acldvppMalloc(&inputDevBuff, PicBufferSize);
  if (aclRet != ACL_SUCCESS) {
    delete[] inputBuff;
    ERROR_LOG("malloc device data buffer failed, aclRet is %d", aclRet);
    fclose(fp);
    return nullptr;
  }
  if (runMode == ACL_HOST) {
    aclRet = aclrtMemcpy(inputDevBuff, PicBufferSize, inputBuff, PicBufferSize, ACL_MEMCPY_HOST_TO_DEVICE);
  }
  else {
    aclRet = aclrtMemcpy(inputDevBuff, PicBufferSize, inputBuff, PicBufferSize, ACL_MEMCPY_DEVICE_TO_DEVICE);
  }
  if (aclRet != ACL_SUCCESS) {
    ERROR_LOG("memcpy from host to device failed, aclRet is %d", aclRet);
    (void)acldvppFree(inputDevBuff);
    delete[] inputBuff;
    fclose(fp);
    return nullptr;
  }

  fclose(fp);
  return reinterpret_cast<char*>(inputDevBuff);
}

Result SaveDvppOutputData(const char* fileName, const void* devPtr, uint32_t dataSize) {
  FILE* outFileFp = fopen(fileName, "wb+");
  if (nullptr == outFileFp) {
    ERROR_LOG("fopen out file %s failed.", fileName);
    return FAILED;
  }
  if (runMode == ACL_HOST) {
    void* hostPtr = nullptr;
    aclError aclRet = aclrtMallocHost(&hostPtr, dataSize);
    if (aclRet != ACL_SUCCESS) {
      ERROR_LOG("malloc host data buffer failed, aclRet is %d", aclRet);
      fclose(outFileFp);
      return FAILED;
    }

    aclRet = aclrtMemcpy(hostPtr, dataSize, devPtr, dataSize, ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
      ERROR_LOG("dvpp output memcpy to host failed, aclRet is %d", aclRet);
      (void)aclrtFreeHost(hostPtr);
      fclose(outFileFp);
      return FAILED;
    }

    size_t writeSize = fwrite(hostPtr, sizeof(char), dataSize, outFileFp);
    if (writeSize != dataSize) {
      ERROR_LOG("need write %u bytes to %s, but only write %zu bytes.",
        dataSize, fileName, writeSize);
      (void)aclrtFreeHost(hostPtr);
      fclose(outFileFp);
      return FAILED;
    }
    (void)aclrtFreeHost(hostPtr);
  }
  else {
    size_t writeSize = fwrite(devPtr, sizeof(char), dataSize, outFileFp);
    if (writeSize != dataSize) {
      ERROR_LOG("need write %u bytes to %s, but only write %zu bytes.",
        dataSize, fileName, writeSize);

      fclose(outFileFp);
      return FAILED;
    }
  }

  fflush(outFileFp);
  fclose(outFileFp);
  return SUCCESS;
}

void DestroyDecodeResource() {
  aclError ret;
  inDevBuffer_ = nullptr;
  if (decodeOutputDesc_ != nullptr) {
    acldvppDestroyPicDesc(decodeOutputDesc_);
    decodeOutputDesc_ = nullptr;
  }
}

void DestroyEncodeResource() {
  if (g_jpegeConfig != nullptr) {
    (void)acldvppDestroyJpegeConfig(g_jpegeConfig);
    g_jpegeConfig = nullptr;
  }
  INFO_LOG("Call acldvppDestroyJpegeConfig success");
  if (encodeDesc_ != nullptr) {
    (void)acldvppDestroyPicDesc(encodeDesc_);
    encodeDesc_ = nullptr;
  }
  INFO_LOG("Call acldvppDestroyPicDesc success");
  if (outDevBuffer_ != nullptr) {
    (void)acldvppFree(outDevBuffer_);
    outDevBuffer_ = nullptr;
  }
  INFO_LOG("Call acldvppFree success");
}


void DestroyResource() {
  aclError ret;

  if (stream_ != nullptr) {
    ret = aclrtDestroyStream(stream_);
    if (ret != ACL_SUCCESS) {
      ERROR_LOG("destroy stream failed");
    }
    stream_ = nullptr;
  }
  INFO_LOG("end to destroy stream");

  if (context_ != nullptr) {
    ret = aclrtDestroyContext(context_);
    if (ret != ACL_SUCCESS) {
      ERROR_LOG("destroy context failed");
    }
    context_ = nullptr;
  }
  INFO_LOG("end to destroy context");

  ret = aclrtResetDevice(deviceId_);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("reset device failed");
  }
  INFO_LOG("end to reset device is %d", deviceId_);

  ret = aclFinalize();
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("finalize acl failed");
  }
  INFO_LOG("end to finalize acl");
}
int main(int argc, char* argv[]) {
  if ((argc < 2) || (argv[1] == nullptr)) {
    ERROR_LOG("Please input: ./main <image_path>");
    return FAILED;
  }
  std::string image_path = std::string(argv[1]);


  //1.AscendCL初始化
  const char* aclConfigPath = "../src/acl.json";
  aclInit(aclConfigPath);
  INFO_LOG("acl init success");
  //2.运行管理资源申请（依次申请Device、Context、Stream）
  aclrtSetDevice(deviceId_);
  INFO_LOG("open device %d success", deviceId_);
  aclrtCreateContext(&context_, deviceId_);
  aclrtCreateStream(&stream_);
  aclrtGetRunMode(&runMode);
  INFO_LOG("create stream success, runMode=%d", runMode);

  //3.创建图片数据处理通道的描述信息和数据处理通道
  dvppChannelDesc_ = acldvppCreateChannelDesc();
  aclError ret = acldvppCreateChannel(dvppChannelDesc_);
  INFO_LOG("dvpp init resource success");

  //4.申请输入内存
  PicDesc pic = { image_path, 0, 0 };
  inDevBuffer_ = GetDeviceBufferOfPicture(pic, inDevBufferSize_);
  if (inDevBuffer_ == nullptr) {
    ERROR_LOG("get pic device buffer failed,index is 0");
    return FAILED;
  }
  inputWidth_ = pic.width;
  inputHeight_ = pic.height;

  //5.申请输出内存
  uint32_t decodeOutBufferSize = 0;
  ret = acldvppPngPredictDecSize(inDevBuffer_, inDevBufferSize_, PIXEL_FORMAT_RGBA_8888, &decodeOutBufferSize);
  ret = acldvppMalloc(&decodeOutDevBuffer_, decodeOutBufferSize);

  //6.创建解码输出图片的描述信息，设置各属性值
  decodeOutputDesc_ = acldvppCreatePicDesc();
  acldvppSetPicDescData(decodeOutputDesc_, decodeOutDevBuffer_);
  acldvppSetPicDescFormat(decodeOutputDesc_, PIXEL_FORMAT_RGBA_8888);
  acldvppSetPicDescSize(decodeOutputDesc_, decodeOutBufferSize);

  //7.执行异步解码，再调用aclrtSynchronizeStream接口阻塞程序运行，直到指定Stream中的所有任务都完成
  ret = acldvppPngDecodeAsync(dvppChannelDesc_, inDevBuffer_, inDevBufferSize_, decodeOutputDesc_, stream_);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("acldvppJpegDecodeAsync failed, ret = %d", ret);
    return FAILED;
  }
  ret = aclrtSynchronizeStream(stream_);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("aclrtSynchronizeStream failed");
    return FAILED;
  }

  //8.获取解码数据的大小
  decodeDataSize_ = acldvppGetPicDescSize(decodeOutputDesc_);
  (void)acldvppFree(inDevBuffer_);
  inDevBuffer_ = nullptr;

  //9.创建编码图片的描述信息
  uint32_t widthAlignment = 16;
  uint32_t heightAlignment = 2;
  uint32_t encodeInWidthStride = AlignmentHelper(inputWidth_, widthAlignment);
  uint32_t encodeInHeightStride = AlignmentHelper(inputHeight_, heightAlignment);
  if (encodeInWidthStride == 0 || encodeInHeightStride == 0) {
    ERROR_LOG("InitEncodeInputDesc AlignmentHelper failed");
    return FAILED;
  }
  encodeDesc_ = acldvppCreatePicDesc();
  INFO_LOG("Call acldvppCreatePicDesc success");
  if (encodeDesc_ == nullptr) {
    ERROR_LOG("acldvppCreatePicDesc g_encodeInputDesc failed");
    return FAILED;
  }

  acldvppSetPicDescData(encodeDesc_, reinterpret_cast<void*>(decodeOutDevBuffer_));
  acldvppSetPicDescFormat(encodeDesc_, PIXEL_FORMAT_RGBA_8888);
  acldvppSetPicDescWidth(encodeDesc_, inputWidth_);
  acldvppSetPicDescHeight(encodeDesc_, inputHeight_);
  acldvppSetPicDescWidthStride(encodeDesc_, encodeInWidthStride);
  acldvppSetPicDescHeightStride(encodeDesc_, encodeInHeightStride);
  acldvppSetPicDescSize(encodeDesc_, decodeDataSize_);

  g_jpegeConfig = acldvppCreateJpegeConfig();
  INFO_LOG("Call acldvppCreateJpegeConfig success");
  acldvppSetJpegeConfigLevel(g_jpegeConfig, 100);

  acldvppJpegPredictEncSize(encodeDesc_, g_jpegeConfig, &outDevBufferSize_);
  aclError aclRet = acldvppMalloc(&outDevBuffer_, outDevBufferSize_);

  if (aclRet != ACL_SUCCESS) {
    ERROR_LOG("malloc encodeOutBufferDev_ failed, aclRet is %d", aclRet);
    return FAILED;
  }

  // call Asynchronous api
  aclRet = acldvppJpegEncodeAsync(dvppChannelDesc_, encodeDesc_, outDevBuffer_, &outDevBufferSize_, g_jpegeConfig, stream_);
  if (aclRet != ACL_SUCCESS) {
    ERROR_LOG("acldvppJpegEncodeAsync failed, aclRet = %d", aclRet);
    return FAILED;
  }
  INFO_LOG("Call acldvppJpegEncodeAsync success");
  aclRet = aclrtSynchronizeStream(stream_);
  if (aclRet != ACL_SUCCESS) {
    ERROR_LOG("encode aclrtSynchronizeStream failed, aclRet = %d", aclRet);
    return FAILED;
  }

  // malloc host mem & save pic
  std::string outfile_path = "test.jpg";
  INFO_LOG("outfile_path = %s", outfile_path.c_str());


  Result res = SaveDvppOutputData(outfile_path.c_str(), outDevBuffer_, outDevBufferSize_);
  if (res != SUCCESS) {
    ERROR_LOG("save encode output data failed.");
    return FAILED;
  }

  DestroyDecodeResource();
  DestroyEncodeResource();
  DestroyResource();

  return 0;
}