#include "acle/aclengine.hpp"
#include "opencv2/opencv.hpp"

#include "seeker/common.h"
#include <iostream>

int deviceId, x, y, angle;

void overlayImageTest(MxBase::AscendStream& stream) {
  auto st = seeker::time::currentTime();
  auto t = seeker::time::currentTime();
  //加载素材图片
  cv::Mat srcMatHost = cv::imread("21.png", cv::IMREAD_UNCHANGED);
  cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);
  cv::resize(srcMatHost, srcMatHost, cv::Size(300, 300));

  I_LOG("load source picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //加载背景图片
  cv::Mat bottomMatHost = cv::imread("bottom.png", cv::IMREAD_COLOR);
  cv::cvtColor(bottomMatHost, bottomMatHost, cv::COLOR_BGR2RGB);

  I_LOG("load background picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传素材图片
  acle::GpuMat srcMatGpu(srcMatHost);
  I_LOG("upload source picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传背景图片
  acle::GpuMat bottomMatGpu(bottomMatHost);
  I_LOG("upload background picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //透明图片叠加
  acle::GpuMat dstMatGpu;
  if (acle::overlayGpuAlpha(srcMatGpu, bottomMatGpu, dstMatGpu, x, y, stream) != 0) return;

  I_LOG("blend tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //下载结果张量至Host侧
  cv::Mat dstMat;
  dstMatGpu.download(dstMat, stream);
  I_LOG("download dst picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  cv::Mat cvtMat;
  cv::cvtColor(dstMat, cvtMat, cv::COLOR_RGB2BGR);
  cv::imwrite("overlay_dst.png", cvtMat);

  I_LOG("write dst mat success, use {}ms", seeker::time::currentTime() - t);
  I_LOG("run time use {} ms", seeker::time::currentTime() - st);
}

void rotateImageTest(MxBase::AscendStream& stream) {
  auto st = seeker::time::currentTime();
  auto t = seeker::time::currentTime();
  //加载素材图片
  cv::Mat srcMatHost = cv::imread("21.png", cv::IMREAD_UNCHANGED);
  cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);
  cv::resize(srcMatHost, srcMatHost, cv::Size(300, 300));

  I_LOG("load source picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传素材图片
  acle::GpuMat srcMatGpu(srcMatHost, 1);
  I_LOG("upload source picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //素材图片旋转
  acle::GpuMat dstMatGpu;
  if (acle::overlayGpuRotate(srcMatGpu, dstMatGpu, angle, stream) != 0) return;

  I_LOG("rotate tensor success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //下载结果张量至Host侧
  cv::Mat dstMat;
  dstMatGpu.download(dstMat, stream);
  I_LOG("download dst picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
  cv::Mat cvtMat;
  cv::cvtColor(dstMat, cvtMat, cv::COLOR_RGBA2BGRA);
  cv::imwrite("rotate_dst.png", cvtMat);

  I_LOG("write dst mat success, use {}ms", seeker::time::currentTime() - t);
  I_LOG("run time use {} ms", seeker::time::currentTime() - st);
}

void maskBlendTest() {
  auto st = seeker::time::currentTime();
  auto t = seeker::time::currentTime();
  //加载蒙版图片
  cv::Mat maskMatHost = cv::imread("mask.png", cv::IMREAD_UNCHANGED);
  //cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);
  cv::resize(maskMatHost, maskMatHost, cv::Size(720, 1280));

  I_LOG("load mask picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //加载视频图片
  cv::Mat videoMatHost = cv::imread("bottom.png", cv::IMREAD_UNCHANGED);
  cv::cvtColor(videoMatHost, videoMatHost, cv::COLOR_BGRA2RGBA);

  I_LOG("load video picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //加载背景图片
  cv::Mat backgroundMatHost = cv::imread("background.png", cv::IMREAD_COLOR);
  cv::cvtColor(backgroundMatHost, backgroundMatHost, cv::COLOR_BGR2RGB);

  I_LOG("load background picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    E_LOG("Please use {./xxx $deviceId $(x or angle) $y(0)} and try again");
    return 0;
  }
  I_LOG("start acle test");
  deviceId = std::atoi(argv[1]);
  angle = x = std::atoi(argv[2]);
  if (argv[3]) y = std::atoi(argv[3]);
  else y = 0;

  MxBase::MxInit();
  {
    acle::GpuMat::setDevice(deviceId);
    MxBase::AscendStream stream(deviceId);
    stream.CreateAscendStream();
    rotateImageTest(stream);
    //overlayImageTest(stream);
  }
  MxBase::MxDeInit();
  I_LOG("acle test success");
	return 0;
}