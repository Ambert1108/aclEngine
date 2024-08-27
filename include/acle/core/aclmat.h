// @brief: 昇腾图像数据结构封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024. 7.24]
// @version: V0.0.1
// @revision: [Ambert@2024.7.24]

#pragma once
#include "utils.h"

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
    Size__(const Size__<_T>& size);
    Size__(const Point__<_T>& pt);

    Size__& operator = (const Size__& sz) = default;
    Size__& operator = (Size__&& sz) noexcept = default;
    friend bool operator == (const Size__<_T>& a, const Size__<_T>& b) {
      return a.width == b.width && a.height == b.height;
    }
    friend bool operator != (const Size__<_T>& a, const Size__<_T>& b) {
      return !(a == b);
    }
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

  template <typename _T> class Scalar_ {
  public:
    Scalar_();
    Scalar_(_T v0, _T v1, _T v2, _T v3 = 0);
    Scalar_(const Scalar_& s);
    Scalar_(Scalar_&& s) noexcept;

    Scalar_& operator=(const Scalar_& s);
    Scalar_& operator=(Scalar_&& s) noexcept;

    static Scalar_<_T> all(_T v0) { return Scalar_<_T>(v0, v0, v0, v0); }
  private:
    _T val[4];

    friend class GpuMat;
  };

  typedef Scalar_<double> Scalar;

  class GpuMat {
  public:
    GpuMat();

    ~GpuMat();

    GpuMat(const GpuMat& gm);

    GpuMat(const GpuMat& gm, const Rect& rect);

    GpuMat& operator=(const GpuMat& gm);

    bool operator==(const GpuMat& gm);

    GpuMat(int rows, int cols, int type, bool flag = false, MxBase::TensorDType dataType = MxBase::TensorDType::UINT8);

    GpuMat(Size size, int type, bool flag = false, MxBase::TensorDType dataType = MxBase::TensorDType::UINT8);

    GpuMat(const cv::Mat& m, bool flag = false, MxBase::TensorDType dataType = MxBase::TensorDType::UINT8);

    //只有data在Device端才可以使用以下构造函数
    GpuMat(int rows, int cols, int type, void* data, size_t dataSize);

    GpuMat(Size size, int type, void* data, size_t dataSize);

    void download(cv::Mat& m, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream());

    void upload(const cv::Mat& m, bool flag = false, MxBase::TensorDType dataType = MxBase::TensorDType::UINT8);

    GpuMat clone(MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream()) const;

    void copyTo(GpuMat& gm, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream()) const;

    bool empty() const;

    Size size() const;

    void release();

    void create(int rows, int cols, int type);

    void create(Size size, int type);

    void setTo(Scalar s);

    int rows, cols, channels;
    size_t step;

    MxBase::Tensor tensor;
  private:
    void* data;
    Size matSize;

    friend class Overlay;
    friend class Transfer;
  };

  //namespace cuda { 
  //  typedef void (*Func)(int32_t);
  //  Func setDevice = GpuMat::setDevice;
  //}
}