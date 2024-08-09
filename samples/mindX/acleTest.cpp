#include "acle/aclengine.hpp"
#include "opencv2/opencv.hpp"

#include "seeker/common.h"
#include <iostream>

int deviceId, x, y, angle;
acle::OverlaySPtr overlay = nullptr;

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
  if (overlay->overlayGpuAlpha(srcMatGpu, bottomMatGpu, dstMatGpu, x, y, stream) != 0) return;

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
  if (overlay->overlayGpuRotate(srcMatGpu, dstMatGpu, angle, stream) != 0) return;

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

void maskBlendTest(MxBase::AscendStream& stream) {
  auto st = seeker::time::currentTime();
  auto t = seeker::time::currentTime();
  //加载蒙版图片
  cv::Mat maskMatHost = cv::imread("mask.png", cv::IMREAD_GRAYSCALE);
  //cv::cvtColor(srcMatHost, srcMatHost, cv::COLOR_BGRA2RGBA);
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

  //上传蒙版图片
  acle::GpuMat maskMatGpu(maskMatHost);
  I_LOG("upload mask picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传视频图片
  acle::GpuMat videoMatGpu(videoMatHost);
  I_LOG("upload video picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //上传背景图片
  acle::GpuMat bgMatGpu(backgroundMatHost);
  I_LOG("upload background picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();

  //蒙版背景叠加
  acle::GpuMat dstMatGpu;
  overlay->blend(videoMatGpu, bgMatGpu, maskMatGpu, dstMatGpu, stream);
  I_LOG("mask blend success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
  
  //下载结果张量至Host侧
  cv::Mat dstMat;
  dstMatGpu.download(dstMat, stream);
  I_LOG("download dst picture success, use {}ms", seeker::time::currentTime() - t);
  t = seeker::time::currentTime();
  
  cv::Mat cvtMat;
  cv::cvtColor(dstMat, cvtMat, cv::COLOR_RGB2BGR);
  cv::imwrite("blend_dst.png", cvtMat);

  I_LOG("write dst mat success, use {}ms", seeker::time::currentTime() - t);
  I_LOG("run time use {} ms", seeker::time::currentTime() - st);
}

int main(int argc, char* argv[]) {
  if (argc < 1) {
    E_LOG("Please use {./xxx $deviceId $(x or angle) $y(0)} and try again");
    return 0;
  }
  I_LOG("start acle test");
  if (argv[1]) deviceId = std::atoi(argv[1]);
  else deviceId = 1;
  if (argv[2]) angle = x = std::atoi(argv[2]);
  else angle = x = 45;
  if (argv[3]) y = std::atoi(argv[3]);
  else y = 0;

  MxBase::MxInit();
  {
    acle::GpuMat::setDevice(deviceId);
    overlay = std::make_shared<acle::Overlay>();
    MxBase::AscendStream stream(deviceId);
    stream.CreateAscendStream();
    maskBlendTest(MxBase::AscendStream::DefaultStream());
    //rotateImageTest(stream);
    //overlayImageTest(stream);
    //using namespace MxBase;
    //int width = 720;
    //int height = 1280;
    //int channels = 1;
    //int step = (size_t)width * channels;
    //size_t size = (size_t)height * step;
    //void* data  = (float*)malloc(size * sizeof(float));
    //Tensor t(data, std::vector<uint32_t>{(uint32_t)height, (uint32_t)width, (uint32_t)channels}, MxBase::TensorDType::FLOAT16);
    //t.ToDevice(deviceId);
    overlay.reset();
  }
  MxBase::MxDeInit();
  I_LOG("acle test success");
	return 0;
}