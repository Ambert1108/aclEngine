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
#include <vector>
#include <cmath>

#define ALIGN_UP(num, align) (((num) + (align) - 1) & ~((align) - 1))

template<typename T>
struct Point {
  T x;
  T y;
};
typedef Point<float> Point2f;

std::vector<std::vector<float>> getRotationMatrix2D(Point2f center, float angle, double scale) {
  float angleRad = angle * M_PI / 180.0;
  float alpha = cos(angleRad);
  float beta = sin(angleRad);

  std::vector<std::vector<float>> rotationMatrix = {
      {alpha, beta, (1 - alpha) * center.x - beta * center.y},
      {-beta, alpha, beta * center.x + (1 - alpha) * center.y}
  };

  return rotationMatrix;
}

void rotateNewSize(int& new_w, int& new_h, int old_w, int old_h, int angle) {
  new_w = fabs(sin(M_PI * (double)(angle / 180.0))) * old_h + fabs(cos(M_PI * (double)(angle / 180.0))) * old_w;
  new_h = fabs(sin(M_PI * (double)(angle / 180.0))) * old_w + fabs(cos(M_PI * (double)(angle / 180.0))) * old_h;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    E_LOG("Please use ./xxx {$deviceId} and try again");
    return 0;
  }
  I_LOG("start warpAffine test");
  int deviceId = std::atoi(argv[1]);

  using namespace MxBase;
  MxInit();
  {
    APP_ERROR result = APP_ERR_OK;

    // MxInit已经包含了设备初始化等工作，无需单独设置
    // MxInit的作用域需要大于图像处理操作
    // 使用Tensor的ToDevice函数上传到设备上时，确认使用哪张卡

    //加载素材图片
    cv::Mat srcMatHost = cv::imread("21.png", cv::IMREAD_UNCHANGED);
    cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);
    cv::resize(srcMatHost, srcMatHost, cv::Size(300, 300));

    I_LOG("load source picture success");

    //将素材图片存入Tensor
    std::vector<uint32_t> srcS{ 1, (uint32_t)srcMatHost.rows, (uint32_t)srcMatHost.cols, 4 };
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

    //计算目标尺寸和旋转矩阵
    int newW = 0;
    int newH = 0;
    rotateNewSize(newW, newH, srcMatHost.cols, srcMatHost.rows, 45);
    Point2f centor{ srcMatHost.cols / 2, srcMatHost.rows / 2 };
    std::vector<std::vector<float>> vec = getRotationMatrix2D(centor, 45, 1.0);
    vec.at(0).at(2) += (float)(newW - srcMatHost.cols) / 2;
    vec.at(1).at(2) += (float)(newH - srcMatHost.rows) / 2;

    //构造结果张量
    size_t dstSize = (size_t)newW * newH;
    void* cpyDstData = malloc(dstSize);
    memset(cpyDstData, 0, dstSize);
    std::vector<uint32_t> dstS{ 1, (uint32_t)newH, (uint32_t)newW, 4 };
    Tensor dstTensor(cpyDstData, dstS, TensorDType::UINT8);
    result = dstTensor.ToDevice(deviceId);
    if (result != APP_ERR_OK) {
      E_LOG("upload destination tensor to device failed");
      return -1;
    }
    I_LOG("build and upload destination tensor success");

    //素材张量旋转
    result = WarpAffineHiper(srcTensor, dstTensor, vec, PaddingMode::PADDING_CONST, 255, WarpAffineMode::INTER_LINEAR);
    if (result != APP_ERR_OK) {
      E_LOG("use WarpAffineHiper failed");
      return -1;
    }

    I_LOG("warp affine tensor success");

    auto dstShape = dstTensor.GetShape();
    for (const auto& each : dstShape) {
      std::cout << each << ", ";
    }

    //下载结果张量至Host侧
    result = dstTensor.ToHost();
    if (result != APP_ERR_OK) {
      E_LOG("download dst tensor to host failed");
      return -1;
    }

    I_LOG("download dst tensor success");

    cv::Mat dstMat(newH, newW, CV_8UC4);
    dstMat.data = (uint8_t*)dstTensor.GetData();
    cv::cvtColor(dstMat, dstMat, cv::COLOR_RGBA2BGRA);
    cv::imwrite("dst.png", dstMat);

    I_LOG("write dst mat success");

    if (cpySrcData) free(cpySrcData);
    if (cpyDstData) free(cpyDstData);
  }
  MxBase::MxDeInit();
  I_LOG("warpAffine test finish");
  return 0;
}