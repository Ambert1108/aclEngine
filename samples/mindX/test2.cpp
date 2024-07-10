#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "opencv2/opencv.hpp"
#include "freeTypeTool.hpp"

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
//#include "acl/acl.h"

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
  //  return -1;
  //}

  using namespace MxBase;
  //MxInitFromConfig("config.json");
  MxInit();
  {
    APP_ERROR result = APP_ERR_OK;
  
	  DeviceContext deviceContext_ = {};
    result = DeviceManager::GetInstance()->InitDevices();
    if (result != APP_ERR_OK) {
      E_LOG("Init device {} failed", deviceId);
      return -1;
    }
    deviceContext_.devId = deviceId;
    result = DeviceManager::GetInstance()->SetDevice(deviceContext_);
    if (result != APP_ERR_OK) {
      E_LOG("Set device {} failed", deviceId);
      return -1;
    }
    I_LOG("init device {} success", deviceId);

    //创建字幕图片
    //std::unique_ptr<freeTypeTool> ftTool = std::make_unique<freeTypeTool>();
    //string text = "你完成录入后要记得将文档存盘,然后把新项目的文件转发给我";
    //cv::Mat textMatHost = cv::Mat(cv::Size(400, 200), CV_8UC4, cv::Scalar::all(0));
    //ftTool->setFont(30, "SourceHanSansCN-Regular.otf");
    //ftTool->draw(textMatHost, cv::Scalar(0, 0, 0, 255), 46, text);
    //cv::Mat cvtMat = cv::Mat(cv::Size(400, 200), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat textMatHost = cv::imread("bottom.png", cv::IMREAD_UNCHANGED);
    cv::cvtColor(textMatHost, textMatHost, cv::COLOR_BGRA2RGBA);

    I_LOG("create caption picture success");

    //加载背景图片并改为四通道类型
    cv::Mat bottomMatHost = cv::imread("bottom.png", cv::IMREAD_COLOR);
    cv::cvtColor(bottomMatHost, bottomMatHost, cv::COLOR_BGR2RGB);

    I_LOG("load background picture success");

    //将字幕图片存入Image
    //std::shared_ptr<uint8_t> data1 = std::make_shared<uint8_t>(*textMatHost.data);
    //uint32_t dataSize1 = textMatHost.rows * textMatHost.step;
    //Size size1 = { textMatHost.cols, textMatHost.rows };
    //Size alignSize1 = { ALIGN_UP(textMatHost.cols, 16), textMatHost.rows };
    //Image textImageHost(data1, dataSize1, -1, std::make_pair(size1, alignSize1), ImageFormat::RGBA_8888);

    //将字幕图片存入Tensor
    std::vector<uint32_t> textS{ (uint32_t)textMatHost.rows, (uint32_t)textMatHost.cols, 4 };
    Tensor textTensor(textMatHost.data, textS, TensorDType::UINT8);

    //I_LOG("fill caption picture in Image success");
    I_LOG("fill caption picture in Tensor success");

    //上传字幕图片至Device侧
    //result = textImageHost.ToDevice(deviceId);
    //if (result != APP_ERR_OK) {
    //  E_LOG("upload text image to device failed");
    //  return -1;
    //}

    //上传字幕向量至Device侧
    result = textTensor.ToDevice(deviceId);
    if (result != APP_ERR_OK) {
      E_LOG("upload text tensor to device failed");
      return -1;
    }

    //I_LOG("upload caption Image success");
    I_LOG("upload caption tensor success");

    //将背景图片存入Image
    //std::shared_ptr<uint8_t> data = std::make_shared<uint8_t>(*bottomMatHost.data);
    //uint32_t dataSize = bottomMatHost.rows * bottomMatHost.step;
    //Size size = { bottomMatHost.cols, bottomMatHost.rows };
    //Size alignSize = { ALIGN_UP(bottomMatHost.cols, 16), bottomMatHost.rows };
    //Image bottomImageHost(data, dataSize, -1, std::make_pair(size, alignSize), ImageFormat::RGB_888);

    //将背景图片存入Tensor
    std::vector<uint32_t> bottomS{ (uint32_t)bottomMatHost.rows, (uint32_t)bottomMatHost.cols, 3 };
    Tensor imageTensor(bottomMatHost.data, bottomS, TensorDType::UINT8);

    //I_LOG("fill background picture in Image success");
    I_LOG("fill background picture in Tensor success");

    //上传背景图片至Device侧
    //result = bottomImageHost.ToDevice(deviceId);
    //if (result != APP_ERR_OK) {
    //  E_LOG("upload bottom image to device failed");
    //  return -1;
    //}

    //上传背景向量至Device侧
    result = imageTensor.ToDevice(deviceId);
    if (result != APP_ERR_OK) {
      E_LOG("upload bottom tensor to device failed");
      return -1;
    }

    //I_LOG("upload background Image success");
    I_LOG("upload background tensor success");

    //Tensor textTensor = textImageHost.ConvertToTensor();
    //Tensor imageTensor = bottomImageHost.ConvertToTensor();
    //std::vector<uint32_t> textS{ 1280, 720, 4 };
    //textTensor.SetShape(textS);
    //std::vector<uint32_t> imageS{ 1280, 720, 3 };
    //imageTensor.SetShape(imageS);
    //auto textShape = textTensor.GetShape();
    //I_LOG("textShape:");
    //for (const auto& c : textShape) {
    //  I_LOG("{}", c);
    //}
    //
    //auto imageShape = imageTensor.GetShape();
    //I_LOG("imageShape:");
    //for (const auto& c : imageShape) {
    //  I_LOG("{}", c);
    //}
    //
    //I_LOG("convert image to tensor success");

    //字幕张量叠加到背景张量
    result = BlendImages(textTensor, imageTensor);
    if (result != APP_ERR_OK) {
      E_LOG("use BlendImages failed");
      return -1;
    }

    I_LOG("blend tensor success");

    //背景张量转换为结果图片
    //Image dstImage;
    //Image::TensorToImage(imageTensor, dstImage, ImageFormat::RGB_888);
    //
    //I_LOG("convert tensor to image success");

    //下载结果图片至Host侧
    //result = bottomImageHost.ToHost();
    //if (result != APP_ERR_OK) {
    //  E_LOG("download dst image to host failed");
    //  return -1;
    //}
    //
    //I_LOG("download dst Image success");

    //下载结果张量至Host侧
    result = imageTensor.ToHost();
    if (result != APP_ERR_OK) {
      E_LOG("download dst tensor to host failed");
      return -1;
    }

    I_LOG("download dst tensor success");

    //cv::Mat dstMat;
    cv::Mat dstMat(bottomMatHost.rows, bottomMatHost.cols, CV_8UC3);
    dstMat.data = (uint8_t*)imageTensor.GetData();
    cv::cvtColor(dstMat, dstMat, cv::COLOR_RGB2BGR);
    cv::imwrite("dst.png", dstMat);

    I_LOG("write dst mat success");


    result = DeviceManager::GetInstance()->DestroyDevices();
    if (result != APP_ERR_OK) {
      E_LOG("destroy device {} failed", deviceId);
      return -1;
    }
  }
  MxDeInit();
	I_LOG("test2 finish");
	return 0;
}