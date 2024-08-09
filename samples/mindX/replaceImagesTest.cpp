#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "opencv2/opencv.hpp"

#include "MxBase/MxBase.h"

//定义mxVision以向量方式处理图像数据时的数据结构
#include "MxBase/E2eInfer/Tensor/Tensor.h"

//定义向量裁剪、裁剪缩放、格式转换功能
#include "MxBase/E2eInfer/Tensor/TensorDvpp.h"

//定义向量仿射变换功能
#include "MxBase/E2eInfer/TensorOperation/TensorWarping.h"

//定义向量背景替换、透明图片叠加功能
#include "MxBase/E2eInfer/TensorOperation/TensorFusion.h"

//定义DVPP侧图像数据使用的数据结构，可以通过该数据结构转换成向量进行处理
#include "MxBase/E2eInfer/Image/Image.h"

//定义硬件设备初始化等功能
#include "MxBase/DeviceManager/DeviceManager.h"
#include "acl/acl.h"

#include "seeker/common.h"
#include <iostream>

#define ALIGN_UP(num, align) (((num) + (align) - 1) & ~((align) - 1))

int deviceId;


using namespace MxBase;

void test() {
  auto st = seeker::time::currentTime();
  auto t = seeker::time::currentTime();
  APP_ERROR result = APP_ERR_OK;
  //加载蒙版图片
  cv::Mat maskMatHost = cv::imread("mask.png", cv::IMREAD_ANYCOLOR);
  I_LOG("mask channel:{}, type={}(reference values CV_8UC1:{}, CV_16FC1:{})", 
    maskMatHost.channels(), maskMatHost.type(), CV_8UC1, CV_16FC1);
  cv::resize(maskMatHost, maskMatHost, cv::Size(720, 1280));
  
  I_LOG("load mask picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
  //加载视频图片
  cv::Mat videoMatHost = cv::imread("frame.png", cv::IMREAD_COLOR);
  cv::cvtColor(videoMatHost, videoMatHost, cv::COLOR_BGR2RGB);

  I_LOG("load video picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //加载背景图片
  cv::Mat backgroundMatHost = cv::imread("background.png", cv::IMREAD_COLOR);
  cv::cvtColor(backgroundMatHost, backgroundMatHost, cv::COLOR_BGR2RGB);

  I_LOG("load background picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //将蒙版图片存入Tensor
  //支持float16，维度支持HW（二维）、HWC（三维）, 当“background”和“replace”的“C”为“1”时，
  // “C”支持“1”，当“background”和“replace”的“C”为3时，“C”支持“1”和“3”
  I_LOG("mask size is w:{} h:{}", maskMatHost.cols, maskMatHost.rows);
  std::vector<uint32_t> maskS{ (uint32_t)maskMatHost.rows, (uint32_t)maskMatHost.cols, 1 };
  void* cpyMaskData = malloc(maskMatHost.rows * maskMatHost.step);
  memcpy(cpyMaskData, maskMatHost.data, maskMatHost.rows * maskMatHost.step);
  Tensor maskTmpTensor(cpyMaskData, maskS, TensorDType::UINT8);

  I_LOG("fill mask picture in Tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传蒙版向量至Device侧
  result = maskTmpTensor.ToDevice(deviceId);
  if (result != APP_ERR_OK) {
    E_LOG("upload mask tensor to device failed");
    return;
  }
  I_LOG("upload mask tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //将视频图片存入Tensor
  //支持float16、uint8类型，维度支持HW（二维）、HWC（三维）、其中“C”（通道数）为“1”或“3”
  I_LOG("video size is w:{} h:{}", videoMatHost.cols, videoMatHost.rows);
  std::vector<uint32_t> videoS{ (uint32_t)videoMatHost.rows, (uint32_t)videoMatHost.cols, 3 };
  void* cpyVideoData = malloc(videoMatHost.rows * videoMatHost.step);
  memcpy(cpyVideoData, videoMatHost.data, videoMatHost.rows * videoMatHost.step);
  Tensor videoTmpTensor(cpyVideoData, videoS, TensorDType::UINT8);

  I_LOG("fill video picture in Tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传视频向量至Device侧
  result = videoTmpTensor.ToDevice(deviceId);
  if (result != APP_ERR_OK) {
    E_LOG("upload video tensor to device failed");
    return;
  }

  I_LOG("upload video tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //将背景图片存入Tensor
  //支持float16、uint8类型，维度支持HW（二维）、HWC（三维）、其中“C”（通道数）为“1”或“3”
  I_LOG("background size is w:{} h:{}", backgroundMatHost.cols, backgroundMatHost.rows);
  std::vector<uint32_t> backgroundS{ (uint32_t)backgroundMatHost.rows, (uint32_t)backgroundMatHost.cols, 3 };
  void* cpyBackgroundData = malloc(backgroundMatHost.rows * backgroundMatHost.step);
  memcpy(cpyBackgroundData, backgroundMatHost.data, backgroundMatHost.rows * backgroundMatHost.step);
  Tensor backgroundTmpTensor(cpyBackgroundData, backgroundS, TensorDType::UINT8);

  I_LOG("fill background picture in Tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传背景向量至Device侧
  result = backgroundTmpTensor.ToDevice(deviceId);
  if (result != APP_ERR_OK) {
    E_LOG("upload background tensor to device failed");
    return;
  }

  I_LOG("upload background tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
  
  Tensor dstTensor, dstTmpTensor;
  //调用背景替换接口
  Tensor maskTmp2Tensor;
  Tensor maskTensor;
  result = ConvertTo(maskTmpTensor, maskTmp2Tensor, TensorDType::FLOAT16);
  if (result != APP_ERR_OK) {
    E_LOG("convert mask tensor to FLOAT16 failed");
    return;
  }

  Tensor oneTensor(maskS, TensorDType::FLOAT16, deviceId);
  Tensor::TensorMalloc(oneTensor);
  oneTensor.SetTensorValue(-1.0f, true);
  result = AbsDiff(maskTmp2Tensor, oneTensor, maskTensor);
  if (result != APP_ERR_OK) {
    E_LOG("AbsDiff mask tensor failed");
    return;
  }

  Tensor videoTensor;
  ConvertTo(videoTmpTensor, videoTensor, TensorDType::FLOAT16);

  Tensor backgroundTensor;
  ConvertTo(backgroundTmpTensor, backgroundTensor, TensorDType::FLOAT16);

  result = BackgroundReplace(videoTensor, backgroundTensor, maskTensor, dstTmpTensor);
  if (result != APP_ERR_OK) {
    E_LOG("use BackgroundReplace failed");
    return;
  }
  ConvertTo(dstTmpTensor, dstTensor, TensorDType::UINT8);
  //I_LOG("dst channel:{}", dstTensor.GetShape().at(2));

  I_LOG("replace tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //下载结果张量至Host侧
  result = dstTensor.ToHost();
  if (result != APP_ERR_OK) {
    E_LOG("download dst tensor to host failed");
    return;
  }

  I_LOG("download dst tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  cv::Mat dstMat(videoMatHost.rows, videoMatHost.cols, CV_8UC3);
  dstMat.data = (uint8_t*)dstTensor.GetData();
  cv::cvtColor(dstMat, dstMat, cv::COLOR_RGB2BGR);
  cv::imwrite("dst.png", dstMat);

  I_LOG("write dst mat success, use {}ms", seeker::time::currentTime() - t);

  if (cpyMaskData) free(cpyMaskData);
  if (cpyVideoData) free(cpyVideoData);
  if (cpyBackgroundData) free(cpyBackgroundData);
  I_LOG("run time {} ms", seeker::time::currentTime() - st);
}

void test2() {
  auto st = seeker::time::currentTime();
  auto t = seeker::time::currentTime();
  APP_ERROR result = APP_ERR_OK;
  //加载蒙版图片
  cv::Mat maskMatHost = cv::imread("mask.png", cv::IMREAD_GRAYSCALE);
  I_LOG("mask channel:{}, type={}(reference values CV_8UC1:{}, CV_16FC1:{})",
    maskMatHost.channels(), maskMatHost.type(), CV_8UC1, CV_16FC1);
  cv::resize(maskMatHost, maskMatHost, cv::Size(720, 1280));

  I_LOG("load mask picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
  //加载视频图片
  cv::Mat videoMatHost = cv::imread("frame.png", cv::IMREAD_COLOR);
  cv::cvtColor(videoMatHost, videoMatHost, cv::COLOR_BGR2RGB);

  I_LOG("load video picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //加载背景图片
  cv::Mat backgroundMatHost = cv::imread("background.png", cv::IMREAD_COLOR);
  cv::cvtColor(backgroundMatHost, backgroundMatHost, cv::COLOR_BGR2RGB);

  I_LOG("load background picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //将蒙版图片存入Tensor
  std::vector<uint32_t> maskS{ (uint32_t)maskMatHost.rows, (uint32_t)maskMatHost.cols, 1 };
  void* cpyMaskData = malloc(maskMatHost.rows * maskMatHost.step);
  memcpy(cpyMaskData, maskMatHost.data, maskMatHost.rows * maskMatHost.step);
  Tensor maskTensor(cpyMaskData, maskS, TensorDType::UINT8);

  I_LOG("fill mask picture in Tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传蒙版向量至Device侧
  result = maskTensor.ToDevice(deviceId);
  if (result != APP_ERR_OK) {
    E_LOG("upload mask tensor to device failed");
    return;
  }
  I_LOG("upload mask tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //将视频图片存入Tensor
  //支持float16、uint8类型，维度支持HW（二维）、HWC（三维）、其中“C”（通道数）为“1”或“3”
  I_LOG("video size is w:{} h:{}", videoMatHost.cols, videoMatHost.rows);
  std::vector<uint32_t> videoS{ (uint32_t)videoMatHost.rows, (uint32_t)videoMatHost.cols, 3 };
  void* cpyVideoData = malloc(videoMatHost.rows * videoMatHost.step);
  memcpy(cpyVideoData, videoMatHost.data, videoMatHost.rows * videoMatHost.step);
  Tensor videoTensor(cpyVideoData, videoS, TensorDType::UINT8);

  I_LOG("fill video picture in Tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传视频向量至Device侧
  result = videoTensor.ToDevice(deviceId);
  if (result != APP_ERR_OK) {
    E_LOG("upload video tensor to device failed");
    return;
  }

  I_LOG("upload video tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //将背景图片存入Tensor
  //支持float16、uint8类型，维度支持HW（二维）、HWC（三维）、其中“C”（通道数）为“1”或“3”
  I_LOG("background size is w:{} h:{}", backgroundMatHost.cols, backgroundMatHost.rows);
  std::vector<uint32_t> backgroundS{ (uint32_t)backgroundMatHost.rows, (uint32_t)backgroundMatHost.cols, 3 };
  void* cpyBackgroundData = malloc(backgroundMatHost.rows * backgroundMatHost.step);
  memcpy(cpyBackgroundData, backgroundMatHost.data, backgroundMatHost.rows * backgroundMatHost.step);
  Tensor backgroundTensor(cpyBackgroundData, backgroundS, TensorDType::UINT8);

  I_LOG("fill background picture in Tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传背景向量至Device侧
  result = backgroundTensor.ToDevice(deviceId);
  if (result != APP_ERR_OK) {
    E_LOG("upload background tensor to device failed");
    return;
  }

  I_LOG("upload background tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  Tensor dstTensor;
  Tensor mask16F;
  Tensor video16F;
  Tensor bg16F;
  Tensor maskDivDst;
  
  //CONVERT_1C8U_TO_1C32F(alphaMask_1C8U, alpha_C1);
  ConvertTo(maskTensor, mask16F, TensorDType::FLOAT16);
  D_LOG("replace --- 1 ---");

  Tensor div(maskS, TensorDType::FLOAT16, deviceId);
  Tensor::TensorMalloc(div);
  div.SetTensorValue(255.0f, true);
  //DivC_1C32F(alpha_C1, 255.0f, alpha_C1);
  Divide(mask16F, div, maskDivDst);
  D_LOG("replace --- 2 ---"); 


  std::vector<Tensor> tv{ maskDivDst.Clone(), maskDivDst.Clone(), maskDivDst.Clone() };
  Tensor mask;
  //DUP_TO_C3(alpha_C1, alpha);
  Merge(tv, mask);
  D_LOG("replace --- 3 ---");

  ConvertTo(videoTensor, video16F, TensorDType::FLOAT16);
  D_LOG("replace --- 4 ---"); 
  ConvertTo(backgroundTensor, bg16F, TensorDType::FLOAT16);
  D_LOG("replace --- 5 ---"); 

  Tensor videoMulDst;
  Multiply(video16F, mask, videoMulDst);
  D_LOG("replace --- 6 ---"); 
  Tensor value(std::vector<uint32_t>{ (uint32_t)maskMatHost.rows, (uint32_t)maskMatHost.cols, 3 }, TensorDType::FLOAT16, deviceId);
  Tensor::TensorMalloc(value);
  value.SetTensorValue(-1.0f, true);
  Tensor maskMulDst1, maskMulDst;
  Multiply(mask, value, maskMulDst1);
  D_LOG("replace --- 7 ---"); 

  value.SetTensorValue(1.0f, true);
  Add(maskMulDst1, value, maskMulDst);
  D_LOG("replace --- 8 ---"); 

  Tensor bgMulDst;
  Multiply(bg16F, maskMulDst, bgMulDst);
  D_LOG("replace --- 9 ---"); 

  Tensor addDst;
  Add(videoMulDst, bgMulDst, addDst);
  D_LOG("replace --- 10 ---"); 

  ConvertTo(addDst, dstTensor, TensorDType::UINT8);
  D_LOG("replace --- 11 ---"); 

  I_LOG("replace tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //下载结果张量至Host侧
  result = dstTensor.ToHost();
  if (result != APP_ERR_OK) {
    E_LOG("download dst tensor to host failed");
    return;
  }

  I_LOG("download dst tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  cv::Mat dstMat(videoMatHost.rows, videoMatHost.cols, CV_8UC3);
  dstMat.data = (uint8_t*)dstTensor.GetData();
  cv::cvtColor(dstMat, dstMat, cv::COLOR_RGB2BGR);
  cv::imwrite("dst.png", dstMat);

  I_LOG("write dst mat success, use {}ms", seeker::time::currentTime() - t);

  if (cpyMaskData) free(cpyMaskData);
  if (cpyVideoData) free(cpyVideoData);
  if (cpyBackgroundData) free(cpyBackgroundData);
  I_LOG("run time {} ms", seeker::time::currentTime() - st);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    E_LOG("Please use ./xxx {$deviceId} and try again");
    return 0;
  }
  I_LOG("start replace test");
  deviceId = std::atoi(argv[1]);
  MxInit();
  {
    test();
  }
  MxDeInit();
  I_LOG("replace test finish");
  return 0;
}