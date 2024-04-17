/*
* Copyright (c) Huawei Technologies Co., Ltd. 2020-2022. All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at

* http://www.apache.org/licenses/LICENSE-2.0

* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <dirent.h>
#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"
#include "main.h"

std::string filePath = "../data/vdec_h265_1frame_rabbit_1280x720.h265";
const int INPUT_WIDTH = 1280;
const int INPUT_HEIGHT = 720;

int32_t deviceId_;
aclrtContext context_;
aclrtStream stream_;
pthread_t threadId_;

int32_t format_ = 1; // 1：YUV420 semi-planner（nv12）; 2：YVU420 semi-planner（nv21）

/* 0：H265 main level
 * 1：H264 baseline level
 * 2：H264 main level
 * 3：H264 high level
 */
int32_t enType_ = 0;

aclvdecChannelDesc* vdecChannelDesc_;
acldvppStreamDesc* streamInputDesc_;
acldvppPicDesc* picOutputDesc_;
void* g_picOutBufferDev;
static bool g_runFlag = true;

bool ReadFileToDeviceMem(const char* fileName, void*& dataDev, uint32_t& dataSize)
{
  // read data from file.
  FILE* fp = fopen(fileName, "rb+");

  fseek(fp, 0, SEEK_END);
  long fileLenLong = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  auto fileLen = static_cast<uint32_t>(fileLenLong);
  void* data = malloc(fileLen);

  size_t readSize = fread(data, 1, fileLen, fp);
  if (readSize < fileLen) {
    free(data);
    fclose(fp);
    return false;
  }

  dataSize = fileLen;
  // Malloc input device memory
  auto aclRet = acldvppMalloc(&dataDev, dataSize);
  // copy input to device memory
  if (runMode == ACL_HOST) {
    aclRet = aclrtMemcpy(dataDev, dataSize, data, fileLen, ACL_MEMCPY_HOST_TO_DEVICE);
  }
  else {
    aclRet = aclrtMemcpy(dataDev, dataSize, data, fileLen, ACL_MEMCPY_DEVICE_TO_DEVICE);
  }
  free(data);
  fclose(fp);
  return true;
}

void* ThreadFunc(aclrtContext sharedContext)
{
  if (sharedContext == nullptr) {
    ERROR_LOG("sharedContext can not be nullptr");
    return reinterpret_cast<void*>(-1);
  }
  INFO_LOG("use shared context for this thread");
  aclError ret = aclrtSetCurrentContext(sharedContext);
  if (ret != ACL_SUCCESS) {
    ERROR_LOG("aclrtSetCurrentContext failed, errorCode = %d", static_cast<int32_t>(ret));
    return reinterpret_cast<void*>(-1);
  }
  INFO_LOG("process callback thread start ");
  while (g_runFlag) {
    /* 异步任务场景下，调用本接口设置超时时间，等待aclrtLaunchCallback接口下发的回调任务执行 */
    // Notice: timeout 1000ms
    aclError aclRet = aclrtProcessReport(1000);
    INFO_LOG("thread wait");
  }
  return nullptr;
}

bool WriteToFile(const char* fileName, const void* dataDev, uint32_t dataSize)
{
  if (dataSize <= 0) {
    ERROR_LOG("dataSize value is abnormal. dataSize=%u\n", dataSize);
    return false;
  }
  void* data = malloc(dataSize);
  if (data == nullptr) {
    ERROR_LOG("malloc data buffer failed. dataSize=%u\n", dataSize);
    return false;
  }

  // copy output to memory
  if (runMode == ACL_HOST) {
    auto aclRet = aclrtMemcpy(data, dataSize, dataDev, dataSize, ACL_MEMCPY_DEVICE_TO_HOST);
  }
  else {
    auto aclRet = aclrtMemcpy(data, dataSize, dataDev, dataSize, ACL_MEMCPY_DEVICE_TO_DEVICE);
  }

  FILE* outFileFp = fopen(fileName, "wb+");

  bool ret = true;
  size_t writeRet = fwrite(data, 1, dataSize, outFileFp);
  if (writeRet != dataSize) {
    ret = false;
  }
  free(data);
  fflush(outFileFp);
  fclose(outFileFp);
  return ret;
}

void callback(acldvppStreamDesc* input, acldvppPicDesc* output, void* userdata)
{
  /* 获取VDEC解码后的输出内存，调用自定义函数WriteToFile进行写入将内存中的数据输
     出到文件中，然后调用acldvppFree接口释放输出存储器 */
  void* vdecOutBufferDev = acldvppGetPicDescData(output);
  uint32_t size = acldvppGetPicDescSize(output);
  static int count = 1;
  std::string fileNameSave = "./output/image" + std::to_string(count) + ".yuv";
  if (!WriteToFile(fileNameSave.c_str(), vdecOutBufferDev, size)) {
    ERROR_LOG("write file failed.");
  }
  aclError ret = acldvppFree(reinterpret_cast<void*>(vdecOutBufferDev));

  // 释放acldvppPicDesc类型数据，代表解码后输出的图片描述数据
  ret = acldvppDestroyPicDesc(output);

  INFO_LOG("call num=%d", count);
  count++;
}

void DestroyResource()
{
  aclrtDestroyStream(stream_);
  stream_ = nullptr;
  aclrtDestroyContext(context_);
  context_ = nullptr;
  aclrtResetDevice(deviceId_);
  aclFinalize();
}

int main()
{
  /* 1.ACL 初始化，不再使用 ACL 相关资源时需要调用 ACL 去初始化aclFinalize接口 */
  const char* aclConfigPath = "../src/acl.json";
  aclError ret = aclInit(aclConfigPath);

  INFO_LOG("vdec use deviceId=%d", deviceId_);
  /* 2.运行管理资源应用，包括Device、Context、Stream */

  /* 指定当前线程中用于运算的Device，同时隐式创建默认Context */
  ret = aclrtSetDevice(deviceId_);

  /**
  * 在当前进程或线程中显式创建一个Context
  * 若不调用aclrtCreateContext接口显式创建Context，那系统会使用默认Context，
    该默认Context是在调用aclrtSetDevice接口时隐式创建的
  * 隐式创建Context：适合简单、无复杂交互逻辑的应用，但缺点在于，在多线程编程中，执行结果取决于线程调度的顺序
  * 显式创建Context：推荐显式，适合大型、复杂交互逻辑的应用，且便于提高程序的可读性、可维护性
  * 在某一进程中指定Device，该进程内的多个线程可共用在此Device上显式创建的Context（调用aclrtCreateContext接口显式创建Context）
  * 若在某一进程内创建多个Context（Context的数量与Stream相关，Stream数量有限制，请参见aclrtCreateStream），当前线程在同一时刻
    内只能使用其中一个Context，建议通过aclrtSetCurrentContext接口明确指定当前线程的Context，增加程序的可维护性
  */
  ret = aclrtCreateContext(&context_, deviceId_);

  /**
  * 每个Context对应一个默认Stream，该默认Stream是调用aclrtSetDevice接口或aclrtCreateContext接口隐式创建的。
    推荐调用aclrtCreateStream接口显式创建Stream
  * 隐式创建Stream：适合简单、无复杂交互逻辑的应用，但缺点在于，在多线程编程中，执行结果取决于线程调度的顺序 
  * 显式创建Stream：推荐显式，适合大型、复杂交互逻辑的应用，且便于提高程序的可读性、可维护性 
  * Atlas 200/300/500 推理产品，硬件资源最多支持1024个Stream，如果已存在多个默认Stream，只能显式创建
    N个Stream（N=1024-默认Stream个数-执行内部同步的Stream个数）
  */
  ret = aclrtCreateStream(&stream_);

  /* 获取当前昇腾AI软件栈的运行模式:DEVICE or HOST */
  aclrtGetRunMode(&runMode);
  INFO_LOG("acl runMode:%d", runMode);

  DIR* dir;
  if ((dir = opendir("./output")) == NULL)
    system("mkdir ./output");

  /* 3.Vdec init */
  // create threadId
  pthread_create(&threadId_, nullptr, ThreadFunc, context_);

  /* 4.创建aclvdecChannelDesc类型的数据，表示创建视频解码处理通道时的通道描述信息。
     如需销毁aclvdecChannelDesc类型的数据，请参见aclvdecDestroyChannelDesc */
   //vdecChannelDesc_ is aclvdecChannelDesc
  vdecChannelDesc_ = aclvdecCreateChannelDesc();

  //调用以下函需要提前调用aclvdecCreateChannelDesc接口创建aclvdecChannelDesc类型的数据
  int channelId = 10;
  /* 设置视频解码处理通道描述信息的属性：解码通道号 */
  ret = aclvdecSetChannelDescChannelId(vdecChannelDesc_, channelId);

  /* 设置视频解码处理通道描述信息的属性：回调线程ID */
  ret = aclvdecSetChannelDescThreadId(vdecChannelDesc_, threadId_);

  /* 设置视频解码处理通道描述信息的属性：回调函数 */
  ret = aclvdecSetChannelDescCallback(vdecChannelDesc_, callback);

  /**
  * 设置视频解码处理通道描述信息的属性：视频编码协议
  * 0：H265_MAIN_LEVEL
  * 1：H264_BASELINE_LEVEL
  * 2：H264_MAIN_LEVEL
  * 3：H264_HIGH_LEVEL
  */
  // 示例中使用H265_MAIN_LEVEL视频编码协议
  ret = aclvdecSetChannelDescEnType(vdecChannelDesc_, static_cast<acldvppStreamFormat>(enType_));

  /**
  * 设置视频解码处理通道描述信息的属性：YUV图像存储格式
  * out_pic_format：int，YUV图像存储格式，支持如下格式：
  *   YUV420SP NV12
  *   YUV420SP NV21
  *   RGB888，Atlas 200/300/500 推理产品不支持该格式
  *   BGR888，Atlas 200/300/500 推理产品不支持该格式
  * 如果不设置输出格式，默认使用YUV420SP NV12
  */
  // PIXEL_FORMAT_YVU_SEMIPLANAR_420
  ret = aclvdecSetChannelDescOutPicFormat(vdecChannelDesc_, static_cast<acldvppPixelFormat>(format_));

  /**
  * 创建视频解码处理的通道，同一个通道可以重复使用，销毁后不再可用，同步接口
  * 通道为非线程安全，即不同线程要求创建不同的通道
  * 通道数最多为256个，见https://www.hiascend.com/document/detail/zh/canncommercial/700/inferapplicationdev/aclcppdevg/aclcppdevg_03_0239.html#:~:text=%E5%85%B1%E7%94%A8%E9%80%9A%E9%81%93%E4%B8%94-,%E9%80%9A%E9%81%93%E6%95%B0%E6%9C%80%E5%A4%9A256,-%EF%BC%8CJPEGE%E4%B8%8EVENC
  */
  ret = aclvdecCreateChannel(vdecChannelDesc_);

  /* Video decoding processing */
  int restLen = 10;
  void* inBufferDev = nullptr;
  uint32_t inBufferSize = 0;
  size_t dataSize = (INPUT_WIDTH * INPUT_HEIGHT * 3) / 2;

  /* 读取文件数据到设备内存 */
  ReadFileToDeviceMem(filePath.c_str(), inBufferDev, inBufferSize);

  // 创建输入视频流描述信息，设置流信息的属性
  streamInputDesc_ = acldvppCreateStreamDesc();
  while (restLen > 0) {

    // inBufferDev表示视频数据输入Device的内存地址，inBufferSize表示内存大小
    ret = acldvppSetStreamDescData(streamInputDesc_, inBufferDev);
    ret = acldvppSetStreamDescSize(streamInputDesc_, inBufferSize);

    // 设备内存g_picOutBufferDev用于存储VDEC解码后的输出数据
    ret = acldvppMalloc(&g_picOutBufferDev, dataSize);

    // 创建输出图像描述信息，设置图像描述信息属性
    // picOutputDesc_ is acldvppPicDesc
    picOutputDesc_ = acldvppCreatePicDesc();
    ret = acldvppSetPicDescData(picOutputDesc_, g_picOutBufferDev);
    ret = acldvppSetPicDescSize(picOutputDesc_, dataSize);
    ret = acldvppSetPicDescFormat(picOutputDesc_, static_cast<acldvppPixelFormat>(format_));


    /**
    * 本接口是异步接口，调用接口成功仅表示任务下发成功，不表示任务执行成功。调用该接口后，
      需调用同步等待接口（例如，    aclrtSynchronizeStream）确保任务已执行完成
    *发送数据前必须保证通道已经被创建，否则返回错误
    *发送码流时须按帧发送，一次只发送完整的一帧码流
    */
    /* 执行视频流解码。 解码完每一帧数据后，系统自动调用callback回调函数将解码后的数据写入
       文件，然后及时释放相关资源 */
    ret = aclvdecSendFrame(vdecChannelDesc_, streamInputDesc_, picOutputDesc_, nullptr, nullptr);

    restLen = restLen - 1;
    INFO_LOG("remaining %d frame", restLen);
  }
  ret = acldvppDestroyStreamDesc(streamInputDesc_);
  ret = aclvdecDestroyChannel(vdecChannelDesc_);
  aclvdecDestroyChannelDesc(vdecChannelDesc_);
  vdecChannelDesc_ = nullptr;

  // destory thread
  g_runFlag = false;
  void* res = nullptr;
  pthread_join(threadId_, &res);
  acldvppFree(inBufferDev);


  DestroyResource();
  return SUCCESS;
}