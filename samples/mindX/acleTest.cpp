#include "acle/aclengine.hpp"
#include "opencv2/opencv.hpp"

#include "seeker/common.h"
#include <iostream>

int deviceId, x, y;

void blendImageTest(MxBase::AscendStream& stream) {
  auto st = seeker::time::currentTime();
  //加载素材图片
  cv::Mat srcMatHost = cv::imread("21.png", cv::IMREAD_UNCHANGED);
  cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);
  cv::resize(srcMatHost, srcMatHost, cv::Size(300, 300));

  I_LOG("load source picture success");

  //加载背景图片
  cv::Mat bottomMatHost = cv::imread("bottom.png", cv::IMREAD_COLOR);
  cv::cvtColor(bottomMatHost, bottomMatHost, cv::COLOR_BGR2RGB);
  I_LOG("load background picture success");

  //上传素材图片
  acle::GpuMat srcMatGpu(srcMatHost);

  //上传背景图片
  acle::GpuMat bottomMatGpu(bottomMatHost);

  //透明图片叠加
  acle::GpuMat dstMatGpu;
  if (acle::overlayGpuAlpha(srcMatGpu, bottomMatGpu, dstMatGpu, x, y, stream) != 0) return;

  I_LOG("blend tensor success");

  //下载结果张量至Host侧
  cv::Mat dstMat;
  dstMatGpu.download(dstMat, stream);
  stream.Synchronize();
  cv::Mat cvtMat;
  cv::cvtColor(dstMat, cvtMat, cv::COLOR_RGB2BGR);
  cv::imwrite("dst.png", cvtMat);

  I_LOG("write dst mat success");
  I_LOG("run time {} ms", seeker::time::currentTime() - st);
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    E_LOG("Please use {./xxx $deviceId $x $y} and try again");
    return 0;
  }
  I_LOG("start acle test");
  deviceId = std::atoi(argv[1]);
  x = std::atoi(argv[2]);
  y = std::atoi(argv[3]);

  MxBase::MxInit();
  {
    acle::GpuMat::setDevice(deviceId);
    MxBase::AscendStream stream(deviceId);
    stream.CreateAscendStream();
    blendImageTest(stream);
    //blendImageTest(stream);
    //blendImageTest(stream);

  }
  MxBase::MxDeInit();
  I_LOG("acle test success");
	return 0;
}