// @brief: 昇腾图像数据结构封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024. 7.24]
// @version: V0.0.1
// @revision: [Ambert@2024.7.24]

#pragma once
#include "utils.hpp"

#include "MxBase/E2eInfer/Tensor/Tensor.h"
#include "opencv2/core/mat.hpp"

#include <iostream>
#include <vector>

namespace acle {
  template<typename _T> class Point__ {
  public:
    Point__();

    Point__(_T _x, _T _y);

    Point__(const Point__& pt) = default;

    Point__(Point__&& pt) noexcept = default;

    Point__& operator = (const Point__& pt) = default;

    Point__& operator = (Point__&& pt) noexcept = default;

    _T x;
    _T y;
  };

  typedef Point__<int> Point2i;
  typedef Point__<int64_t> Point2l;
  typedef Point__<float> Point2f;
  typedef Point__<double> Point2d;
  typedef Point2i Point;

  template<typename _T> class Size__ {
  public:
    Size__();
    Size__(_T _width, _T _height);
    Size__(const Point__<_T>& pt);

    Size__& operator = (const Size__& sz) = default;
    Size__& operator = (Size__&& sz) noexcept = default;
    bool empty() const;


    _T width;
    _T height;
  };

  typedef Size__<int> Size2i;
  typedef Size__<int64_t> Size2l;
  typedef Size__<float> Size2f;
  typedef Size__<double> Size2d;
  typedef Size2i Size;

  typedef MxBase::Rect Rect;

  class GpuMat {
  public:
    GpuMat();

    ~GpuMat();

    GpuMat(const GpuMat& gm);

    GpuMat(const GpuMat& gm, const Rect& rect);

    GpuMat& operator=(const GpuMat& gm);

    bool operator==(const GpuMat& gm);

    GpuMat(int rows, int cols, int type);

    GpuMat(Size size, int type);

    GpuMat(const cv::Mat& m);

    static void setDevice(int32_t id);

    void download(cv::Mat& m);

    void upload(const cv::Mat& m);

    GpuMat clone(MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream()) const;

    void copyTo(GpuMat& gm, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream()) const;

    bool empty() const;

    int rows, cols, channels;
    size_t step;

    MxBase::Tensor tensor;
  private:
    static int32_t deviceId;
    void* data;
  };

  //namespace cuda { 
  //  typedef void (*Func)(int32_t);
  //  Func setDevice = GpuMat::setDevice;
  //}
}