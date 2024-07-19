#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "opencv2/opencv.hpp"
//#include <opencv2/opencv.hpp>
//#include "freeTypeTool.hpp"

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

#include <iostream>

#define ALIGN_UP(num, align) (((num) + (align) - 1) & ~((align) - 1))

int main(int argc, char* argv[]) {
  if (argc < 2) {
    E_LOG("Please use ./xxx {$deviceId} and try again");
    return 0;
  }
  I_LOG("start test2");
  int deviceId = std::atoi(argv[1]);

  //aclError ret = aclrtSetDevice(deviceId);
  //if (ret != ACL_SUCCESS) {
  //  E_LOG("Acl open device {} failed, errorCode is {}", deviceId, ret);
  //  return ACL_ERROR_NONE;
  //}
  //I_LOG("Open device {} ok", deviceId);

  using namespace MxBase;
  //MxInitFromConfig("config.json");
  MxInit();
  {
    APP_ERROR result = APP_ERR_OK;

    //DeviceContext deviceContext_ = {};
    //result = DeviceManager::GetInstance()->InitDevices();
    //if (result != APP_ERR_OK) {
    //  E_LOG("Init device {} failed", deviceId);
    //  return -1;
    //}
    //deviceContext_.devId = deviceId;
    //result = DeviceManager::GetInstance()->SetDevice(deviceContext_);
    //if (result != APP_ERR_OK) {
    //  E_LOG("Set device {} failed", deviceId);
    //  return -1;
    //}
    //I_LOG("init device {} success", deviceId);

    //加载素材图片
    //cv::Mat srcMatHost = cv::imread("top.png", cv::IMREAD_UNCHANGED);
    cv::Mat srcMatHost = cv::imread("21.png", cv::IMREAD_UNCHANGED);
    cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);

    I_LOG("load source picture success");

    //加载背景图片
    cv::Mat bottomMatHost = cv::imread("bottom.png", cv::IMREAD_COLOR);
    cv::cvtColor(bottomMatHost, bottomMatHost, cv::COLOR_BGR2RGB);

    I_LOG("load background picture success");

    //将素材图片存入Tensor
    std::vector<uint32_t> srcS{ (uint32_t)srcMatHost.rows, (uint32_t)srcMatHost.cols, 4 };
    void* cpySrcData = malloc(srcMatHost.rows * srcMatHost.step);
    memcpy(cpySrcData, srcMatHost.data, srcMatHost.rows * srcMatHost.step);
    Tensor srcTensor(cpySrcData, srcS, TensorDType::UINT8);

    I_LOG("fill source picture in Tensor success");

    //上传素材向量至Device侧
    result = srcTensor.ToDevice(deviceId);
    if (result != APP_ERR_OK) {
      E_LOG("upload source tensor to device failed");
      return -1;
    }

    I_LOG("upload source tensor success");

    //将背景图片存入Tensor
    std::vector<uint32_t> bottomS{ (uint32_t)bottomMatHost.rows, (uint32_t)bottomMatHost.cols, 3 };
    void* cpyBottomData = malloc(bottomMatHost.rows * bottomMatHost.step);
    memcpy(cpyBottomData, bottomMatHost.data, bottomMatHost.rows * bottomMatHost.step);
    Tensor imageTensor(cpyBottomData, bottomS, TensorDType::UINT8);

    I_LOG("fill background picture in Tensor success");

    //上传背景向量至Device侧
    result = imageTensor.ToDevice(deviceId);
    if (result != APP_ERR_OK) {
      E_LOG("upload bottom tensor to device failed");
      return -1;
    }

    I_LOG("upload background tensor success");

    //素材张量叠加到背景张量
    result = BlendImages(srcTensor, imageTensor);
    if (result != APP_ERR_OK) {
      E_LOG("use BlendImages failed");
      return -1;
    }

    I_LOG("blend tensor success");

    //下载结果张量至Host侧
    result = imageTensor.ToHost();
    if (result != APP_ERR_OK) {
      E_LOG("download dst tensor to host failed");
      return -1;
    }

    I_LOG("download dst tensor success");

    cv::Mat dstMat(bottomMatHost.rows, bottomMatHost.cols, CV_8UC3);
    dstMat.data = (uint8_t*)imageTensor.GetData();
    cv::cvtColor(dstMat, dstMat, cv::COLOR_RGB2BGR);
    cv::imwrite("dst.png", dstMat);

    I_LOG("write dst mat success");


    //result = DeviceManager::GetInstance()->DestroyDevices();
    //if (result != APP_ERR_OK) {
    //  E_LOG("destroy device {} failed", deviceId);
    //  return -1;
    //}

    if(cpySrcData) free(cpySrcData);
    if(cpyBottomData) free(cpyBottomData);
  }
  MxDeInit();
  I_LOG("test2 finish");
  return 0;
}