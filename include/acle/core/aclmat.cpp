#include "aclmat.h"

namespace acle {
	template<typename _T> inline Point__<_T>::Point__() : x(0), y(0) {};
	template<typename _T> inline Point__<_T>::Point__(_T _x, _T _y) : x(_x), y(_y) {};

	template<typename _T> inline Size__<_T>::Size__() : width(0), height(0) {};
	template<typename _T> inline Size__<_T>::Size__(_T _width, _T _height) : width(_width), height(_height) {};
	template<typename _T> inline Size__<_T>::Size__(const Point__<_T>& pt) : width(pt.x), height(pt.y) {};

  GpuMat::GpuMat() : rows(0), cols(0), channels(0), tensor(nullptr), data(nullptr) {};

  GpuMat::~GpuMat() {
    if (data) {
      free(data);
      data = nullptr;
    }
  };

  GpuMat::GpuMat(const GpuMat& gm) {
    this->rows = gm.rows;
    this->cols = gm.cols;
    this->channels = gm.channels;
    this->tensor = std::make_shared<MxBase::Tensor>(*gm.tensor.get());
  }

  GpuMat::GpuMat(const GpuMat& gm, const Rect& rect) {
    this->rows = gm.rows;
    this->cols = gm.cols;
    this->channels = gm.channels;
    this->tensor = std::make_shared<MxBase::Tensor>(*gm.tensor.get(), rect);
  }

  GpuMat& GpuMat::operator=(const GpuMat & gm) {
    this->rows = gm.rows;
    this->cols = gm.cols;
    this->channels = gm.channels;
    this->tensor = gm.tensor;
  }

  bool GpuMat::operator==(const GpuMat& gm) {
    return *this->tensor.get() == *gm.tensor.get();
  }

  GpuMat::GpuMat(int rows, int cols, int type) {
    this->rows = rows;
    this->cols = cols;
    switch (type) {
    case ACLE_8UC1:
      this->channels = 1;
      break;
    case ACLE_8UC3:
      this->channels = 3;
      break;
    case ACLE_8UC4:
      this->channels = 4;
      break;
    default:
      this->channels = 3;
      break;
    }
    this->tensor = std::make_shared<MxBase::Tensor>
      (std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels }, 
        MxBase::TensorDType::UINT8);
    this->tensor->ToDevice(deviceId);
  }

  GpuMat::GpuMat(Size size, int type) {
    this->rows = size.height;
    this->cols = size.width;
    switch (type) {
    case ACLE_8UC1:
      this->channels = 1;
      break;
    case ACLE_8UC3:
      this->channels = 3;
      break;
    case ACLE_8UC4:
      this->channels = 4;
      break;
    default:
      this->channels = 3;
      break;
    }
    this->tensor = std::make_shared<MxBase::Tensor>
      (std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels },
        MxBase::TensorDType::UINT8);
    this->tensor->ToDevice(deviceId);
  }

  GpuMat::GpuMat(const cv::Mat& m) {
    this->rows = m.rows;
    this->cols = m.cols;
    switch (m.type()) {
    case ACLE_8UC1:
      this->channels = 1;
      break;
    case ACLE_8UC3:
      this->channels = 3;
      break;
    case ACLE_8UC4:
      this->channels = 4;
      break;
    default:
      this->channels = 3;
      break;
    }
    if (data) free(data);
    data = malloc(m.rows * m.step);
    memcpy(data, m.data, m.rows * m.step);
    this->tensor = std::make_shared<MxBase::Tensor>
      (data,
       std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels },
       MxBase::TensorDType::UINT8);
    this->tensor->ToDevice(deviceId);
  }

  int32_t GpuMat::deviceId = -1;

  void GpuMat::setDevice(int32_t id) { deviceId = id; }

  void GpuMat::download(GpuMat& gm) {
    gm = this->clone();
    gm.tensor->ToHost();
  }

  void GpuMat::upload(GpuMat& gm) {
    gm = this->clone();
    gm.tensor->ToDevice(deviceId);
  }

  GpuMat GpuMat::clone() const {
    GpuMat gm;
    *gm.tensor.get() = this->tensor->Clone();
    return gm;
  }

  void GpuMat::copyTo(const GpuMat& gm) {
    this->tensor->Clone(*gm.tensor.get());
  }
}