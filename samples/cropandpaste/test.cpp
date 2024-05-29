#include <iostream>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <map>
#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"
#include <cstdint>

#define INFO_LOG(fmt, args...) fprintf(stdout, "[INFO]  " fmt "\n", ##args)
#define WARN_LOG(fmt, args...) fprintf(stdout, "[WARN]  " fmt "\n", ##args)
#define ERROR_LOG(fmt, args...) fprintf(stderr, "[ERROR] " fmt "\n", ##args)

typedef enum Result {
  SUCCESS = 0,
  FAILED = 1
} Result;

typedef struct PicDesc {
  std::string picName;
  uint32_t width;
  uint32_t height;
  uint32_t jpegDecodeSize;
}PicDesc;

int32_t deviceId_;
aclrtContext context_;
aclrtStream stream_;

aclvdecChannelDesc* vdecChannelDesc_;
acldvppStreamDesc* streamInputDesc_;
acldvppPicDesc* picOutputDesc_;
acldvppChannelDesc* dvppChannelDesc_;
uint32_t inBufferSize;
uint32_t inputWidth;
uint32_t inputHeight;
aclrtRunMode runMode;


void* decodeOutDevBuffer_; // decode output buffer
acldvppPicDesc* decodeOutputDesc_; //decode output desc
uint32_t decodeDataSize_;

PicDesc inPicDesc;
PicDesc outPicDesc;

using namespace std;

/* Run managed resource applications, including Device, Context, and Stream*/
Result Initparam(int argc, char* argv[])
{
  DIR* dir;
  if ((dir = opendir("./output")) == NULL)
    system("mkdir ./output");
  if (argc != 7) {
    ERROR_LOG("./resize infile w h outfile w h");
    return FAILED;
  }
  int inw, inh, outw, outh;
  inw = atoi(argv[2]);
  inh = atoi(argv[3]);
  outw = atoi(argv[5]);
  outh = atoi(argv[6]);
  inPicDesc = { argv[1],(uint32_t)inw,(uint32_t)inh };
  outPicDesc = { argv[4],(uint32_t)outw,(uint32_t)outh };
  return SUCCESS;
}

uint32_t AlignmentHelper(uint32_t origSize, uint32_t alignment)
{
  if (alignment == 0) {
    return 0;
  }
  uint32_t alignmentH = alignment - 1;
  return (origSize + alignmentH) / alignment * alignment;
}

uint32_t SaveOutputFile(const char* fileName, const void* devPtr, uint32_t dataSize)
{
  FILE* outFileFp = fopen(fileName, "wb+");
  if (runMode == ACL_HOST) {
    void* hostPtr = nullptr;
    aclrtMallocHost(&hostPtr, dataSize);
    aclrtMemcpy(hostPtr, dataSize, devPtr, dataSize, ACL_MEMCPY_DEVICE_TO_HOST);
    fwrite(hostPtr, sizeof(char), dataSize, outFileFp);
    (void)aclrtFreeHost(hostPtr);
  }
  else {
    fwrite(devPtr, sizeof(char), dataSize, outFileFp);
  }
  fflush(outFileFp);
  fclose(outFileFp);
  return SUCCESS;
}

char* ReadInputFile(std::string fileName, uint32_t& fileSize)
{
  std::ifstream binFile(fileName, std::ifstream::binary);
  if (binFile.is_open() == false) {
    ERROR_LOG("open file %s failed", fileName.c_str());
    return nullptr;
  }

  binFile.seekg(0, binFile.end);
  uint32_t binFileBufferLen = binFile.tellg();
  if (binFileBufferLen == 0) {
    ERROR_LOG("binfile is empty, filename is %s", fileName.c_str());
    binFile.close();
    return nullptr;
  }

  binFile.seekg(0, binFile.beg);

  char* binFileBufferData = new(std::nothrow) char[binFileBufferLen];
  if (binFileBufferData == nullptr) {
    ERROR_LOG("malloc binFileBufferData failed");
    binFile.close();
    return nullptr;
  }
  binFile.read(binFileBufferData, binFileBufferLen);
  binFile.close();
  fileSize = binFileBufferLen;
  return binFileBufferData;
}

void* GetDeviceBufferOfPicture(PicDesc& picDesc, uint32_t& devPicBufferSize)
{
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

  acldvppJpegFormat format;

  aclError aclRet = acldvppJpegGetImageInfoV2(inputBuff, inputBuffSize, &picDesc.width, &picDesc.height,
    nullptr, &format);
  if (aclRet != ACL_SUCCESS) {
    ERROR_LOG("get jpeg image info failed, errorCode is %d", static_cast<int32_t>(aclRet));
    delete[] inputBuff;
    fclose(fp);
    return nullptr;
  }

  INFO_LOG("get jpeg image info successed, width=%d, height=%d, format=%d, jpegDecodeSize=%d", picDesc.width, picDesc.height, format, picDesc.jpegDecodeSize);

  // when you run, from the output, we can see that the original format is ACL_JPEG_CSS_420, 
  // so it can be decoded as PIXEL_FORMAT_YUV_SEMIPLANAR_420 or PIXEL_FORMAT_YVU_SEMIPLANAR_420
  aclRet = acldvppJpegPredictDecSize(inputBuff, inputBuffSize, PIXEL_FORMAT_YUV_SEMIPLANAR_420, &picDesc.jpegDecodeSize);
  if (aclRet != ACL_SUCCESS) {
    ERROR_LOG("get jpeg decode size failed, errorCode is %d", static_cast<int32_t>(aclRet));
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

int main(int argc, char* argv[])
{
  /* 1.ACL initialization */
  const char* aclConfigPath = "../src/acl.json";
  aclInit(aclConfigPath);

  /* 2.Run the management resource application, including Device, Context, Stream */

  aclrtSetDevice(deviceId_);
  aclrtCreateContext(&context_, deviceId_);
  aclrtCreateStream(&stream_);
  aclrtGetRunMode(&runMode);

  /* 3.Initialization parameters */
  Initparam(argc, argv);
  const int outputWidth = outPicDesc.width; // cur model shape is 224 * 224
  const int outputHeight = outPicDesc.height;

  /* 4. Channel description information when creating image data processing channels, dvppChannelDesc_ is acldvppChannelDesc type*/
  dvppChannelDesc_ = acldvppCreateChannelDesc();

  /* 5. Create the image data processing channel.*/
  acldvppCreateChannel(dvppChannelDesc_);

  // GetPicDevBuffer4JpegD
  //uint32_t inputBuffSize = 0;
  //char* inputBuff = ReadInputFile(inPicDesc.picName, inputBuffSize);
  //void* inBufferDev = nullptr;
  //inBufferSize = inputBuffSize;
  //acldvppMalloc(&inBufferDev, inBufferSize);
  //if (runMode == ACL_HOST) {
  //  aclrtMemcpy(inBufferDev, inBufferSize, inputBuff, inputBuffSize, ACL_MEMCPY_HOST_TO_DEVICE);
  //}
  //else {
  //  aclrtMemcpy(inBufferDev, inBufferSize, inputBuff, inputBuffSize, ACL_MEMCPY_DEVICE_TO_DEVICE);
  //}
  //delete[] inputBuff;

  uint32_t devPicBufferSize;
  void* picDevBuffer = GetDeviceBufferOfPicture(inPicDesc, devPicBufferSize);
  if (picDevBuffer == nullptr) {
    ERROR_LOG("get pic device buffer failed,index is 0");
    return FAILED;
  }
  //4.Create image data processing channel
  dvppChannelDesc_ = acldvppCreateChannelDesc();
  acldvppCreateChannel(dvppChannelDesc_);
  INFO_LOG("dvpp init resource success");

  // InitDecodeOutputDesc
  uint32_t alignWidth;
  uint32_t alignHeight;
  uint32_t decodeOutWidthStride;
  uint32_t decodeOutHeightStride;
  auto socVersion = aclrtGetSocName();
  if (strncmp(socVersion, "Ascend310P3", sizeof("Ascend310P3") - 1) == 0) {
    alignWidth = (inPicDesc.width + 1) / 2 * 2; // 2-byte alignment
    alignHeight = (inPicDesc.height + 1) / 2 * 2; // 2-byte alignment
    decodeOutWidthStride = (inPicDesc.width + 63) / 64 * 64; // 64-byte alignment
    decodeOutHeightStride = (inPicDesc.height + 15) / 16 * 16; // 16-byte alignment
    INFO_LOG("Ascend310P3: width=%d, width stride=%d, height=%d, height stride=%d",
      alignWidth, decodeOutWidthStride, alignHeight, decodeOutHeightStride);
  }
  else {
    alignWidth = inPicDesc.width;
    alignHeight = inPicDesc.height;
    decodeOutWidthStride = (inPicDesc.width + 127) / 128 * 128; // 128-byte alignment
    decodeOutHeightStride = (inPicDesc.height + 15) / 16 * 16; // 16-byte alignment
    INFO_LOG("Ascend310I Pro: width=%d, height=%d", alignWidth, alignHeight);
  }
  // use acldvppJpegPredictDecSize to get output size.
  // uint32_t decodeOutBufferSize = decodeOutWidthStride * decodeOutHeightStride * 3 / 2; // yuv format size
  // uint32_t decodeOutBufferSize = testPic.jpegDecodeSize;
  aclError ret = acldvppMalloc(&decodeOutDevBuffer_, inPicDesc.jpegDecodeSize);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("acldvppMalloc jpegOutBufferDev failed, ret = %d", ret);
    return FAILED;
  }

  decodeOutputDesc_ = acldvppCreatePicDesc();
  if (decodeOutputDesc_ == nullptr) {
    ERROR_LOG("acldvppCreatePicDesc decodeOutputDesc failed");
    return FAILED;
  }

  acldvppSetPicDescData(decodeOutputDesc_, decodeOutDevBuffer_);
  // here the format shoud be same with the value you set when you get decodeOutBufferSize from
  acldvppSetPicDescFormat(decodeOutputDesc_, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
  acldvppSetPicDescWidth(decodeOutputDesc_, alignWidth);
  acldvppSetPicDescHeight(decodeOutputDesc_, alignHeight);
  acldvppSetPicDescWidthStride(decodeOutputDesc_, decodeOutWidthStride);
  acldvppSetPicDescHeightStride(decodeOutputDesc_, decodeOutHeightStride);
  acldvppSetPicDescSize(decodeOutputDesc_, inPicDesc.jpegDecodeSize);

  ret = acldvppJpegDecodeAsync(dvppChannelDesc_, picDevBuffer, devPicBufferSize,
    decodeOutputDesc_, stream_);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("acldvppJpegDecodeAsync failed, ret = %d", ret);
    return FAILED;
  }

  ret = aclrtSynchronizeStream(stream_);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("aclrtSynchronizeStream failed");
    return FAILED;
  }

  uint32_t oddNum = 1;
  uint32_t cropSizeWidth = 320;
  uint32_t cropSizeHeight = 150;
  uint32_t cropLeftOffset = 0;  // must even
  uint32_t cropRightOffset = cropLeftOffset + cropSizeWidth - oddNum;  // must odd
  uint32_t cropTopOffset = 0;  // must even
  uint32_t cropBottomOffset = cropTopOffset + cropSizeHeight - oddNum;  // must odd
  acldvppRoiConfig* cropArea_ = acldvppCreateRoiConfig(cropLeftOffset, cropRightOffset,
    cropTopOffset, cropBottomOffset);

  uint32_t pasteLeftOffset = 16;  // must even
  uint32_t pasteRightOffset = pasteLeftOffset + cropSizeWidth - oddNum;  // must odd
  uint32_t pasteTopOffset = 200;  // must even
  uint32_t pasteBottomOffset = pasteTopOffset + cropSizeHeight - oddNum;  // must odd
  acldvppRoiConfig* pasteArea_ = acldvppCreateRoiConfig(pasteLeftOffset, pasteRightOffset,
    pasteTopOffset, pasteBottomOffset);

  uint32_t widthAlignment = 16;
  uint32_t heightAlignment = 2;
  uint32_t sizeAlignment = 3;
  uint32_t sizeNum = 2;
  uint32_t inputWidth = inPicDesc.width;
  uint32_t inputHeight = inPicDesc.height;
  uint32_t jpegOutWidthStride = AlignmentHelper(inputWidth, widthAlignment);
  uint32_t jpegOutHeightStride = AlignmentHelper(inputHeight, heightAlignment);
  uint32_t jpegOutBufferSize = jpegOutWidthStride * jpegOutHeightStride * sizeAlignment / sizeNum;

  void* vpcOutBufferDev_ = nullptr;
  int dvppOutWidth = inPicDesc.width;
  int dvppOutHeight = inPicDesc.height;
  int dvppOutWidthStride = AlignmentHelper(dvppOutWidth, widthAlignment);
  int dvppOutHeightStride = AlignmentHelper(dvppOutHeight, heightAlignment);
  //int dvppOutWidth = outputWidth;
  //int dvppOutHeight = outputHeight;
  //int dvppOutWidthStride = AlignmentHelper(outputWidth, widthAlignment);
  //int dvppOutHeightStride = AlignmentHelper(outputHeight, heightAlignment);
  uint32_t vpcOutBufferSize_ = dvppOutWidthStride * dvppOutHeightStride * sizeAlignment / sizeNum;
  aclError aclRet = acldvppMalloc(&vpcOutBufferDev_, vpcOutBufferSize_);
  ret = aclrtMemcpy(vpcOutBufferDev_, vpcOutBufferSize_, decodeOutDevBuffer_, jpegOutBufferSize,
    ACL_MEMCPY_DEVICE_TO_DEVICE);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("memcpy failed. Input host buffer size is %u",
      vpcOutBufferSize_);
    acldvppFree(vpcOutBufferDev_);
    return -1;
  }

  acldvppPicDesc* vpcInputDesc_ = acldvppCreatePicDesc();
  acldvppSetPicDescData(vpcInputDesc_, decodeOutDevBuffer_); // JpegD -> vpcCropAndPaste
  acldvppSetPicDescFormat(vpcInputDesc_, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
  acldvppSetPicDescWidth(vpcInputDesc_, inputWidth);
  acldvppSetPicDescHeight(vpcInputDesc_, inputHeight);
  acldvppSetPicDescWidthStride(vpcInputDesc_, jpegOutWidthStride);
  acldvppSetPicDescHeightStride(vpcInputDesc_, jpegOutHeightStride);
  acldvppSetPicDescSize(vpcInputDesc_, jpegOutBufferSize);
  INFO_LOG("JpegPicDesc w=%d/%d,h=%d/%d,vpcOutBufferSize=%u", inputWidth, jpegOutWidthStride, inputHeight, jpegOutHeightStride, jpegOutBufferSize);

  acldvppPicDesc* vpcOutputDesc_ = acldvppCreatePicDesc();
  INFO_LOG("acldvppCreatePicDesc w=%d/%d,h=%d/%d,vpcOutBufferSize=%u", dvppOutWidth, dvppOutWidthStride, dvppOutHeight, dvppOutHeightStride, vpcOutBufferSize_);
  acldvppSetPicDescData(vpcOutputDesc_, vpcOutBufferDev_);
  acldvppSetPicDescFormat(vpcOutputDesc_, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
  acldvppSetPicDescWidth(vpcOutputDesc_, dvppOutWidth);
  acldvppSetPicDescHeight(vpcOutputDesc_, dvppOutHeight);
  acldvppSetPicDescWidthStride(vpcOutputDesc_, dvppOutWidthStride);
  acldvppSetPicDescHeightStride(vpcOutputDesc_, dvppOutHeightStride);
  acldvppSetPicDescSize(vpcOutputDesc_, vpcOutBufferSize_);

  // crop and patse pic
  acldvppVpcCropAndPasteAsync(dvppChannelDesc_, vpcInputDesc_,
    vpcOutputDesc_, cropArea_, pasteArea_, stream_);
  aclrtSynchronizeStream(stream_);

  (void)acldvppDestroyRoiConfig(cropArea_);
  cropArea_ = nullptr;
  (void)acldvppDestroyRoiConfig(pasteArea_);
  pasteArea_ = nullptr;
  (void)acldvppDestroyPicDesc(vpcInputDesc_);
  vpcInputDesc_ = nullptr;
  (void)acldvppDestroyPicDesc(vpcOutputDesc_);
  vpcOutputDesc_ = nullptr;
  (void)acldvppDestroyChannel(dvppChannelDesc_);
  (void)acldvppDestroyChannelDesc(dvppChannelDesc_);
  dvppChannelDesc_ = nullptr;

  SaveOutputFile(outPicDesc.picName.c_str(), vpcOutBufferDev_, vpcOutBufferSize_);
  if (vpcOutBufferDev_ != nullptr) {
    (void)acldvppFree(vpcOutBufferDev_);
    vpcOutBufferDev_ = nullptr;
  }
  aclrtDestroyStream(stream_);
  stream_ = nullptr;
  aclrtDestroyContext(context_);
  context_ = nullptr;
  aclrtResetDevice(deviceId_);
  INFO_LOG("end to reset device is %d", deviceId_);

  aclFinalize();
  INFO_LOG("end to finalize acl");

  return SUCCESS;
}