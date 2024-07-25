#include "aclmat.h"

namespace acle {
	template<typename _T> inline Point__<_T>::Point__() : x(0), y(0) {};
	template<typename _T> inline Point__<_T>::Point__(_T _x, _T _y) : x(_x), y(_y) {};

	template<typename _T> inline Size__<_T>::Size__() : width(0), height(0) {};
	template<typename _T> inline Size__<_T>::Size__(_T _width, _T _height) : width(_width), height(_height) {};
	template<typename _T> inline Size__<_T>::Size__(const Point__<_T>& pt) : width(pt.x), height(pt.y) {};

  GpuMat::GpuMat() : rows(0), cols(0), channels(0), data(nullptr) {};

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
    this->tensor = gm.tensor;
    data = nullptr;
  }

  GpuMat::GpuMat(const GpuMat& gm, const Rect& rect) {
    this->rows = gm.rows;
    this->cols = gm.cols;
    this->channels = gm.channels;
    this->tensor = MxBase::Tensor(gm.tensor, rect);
    data = nullptr;
  }

  GpuMat& GpuMat::operator=(const GpuMat & gm) {
    this->rows = gm.rows;
    this->cols = gm.cols;
    this->channels = gm.channels;
    this->tensor = gm.tensor;
    if (gm.data) {
      if (data) free(data);
      data = malloc(gm.rows * gm.step);
      memcpy(data, gm.data, gm.rows * gm.step);
    }
  }

  bool GpuMat::operator==(const GpuMat& gm) {
    return this->tensor == gm.tensor;
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
    this->tensor = MxBase::Tensor(std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels },MxBase::TensorDType::UINT8);
    this->tensor.ToDevice(deviceId);
    data = nullptr;
  }

  GpuMat::GpuMat(Size size, int type) : GpuMat(size.height, size.width, type) {}

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
    this->step = m.step;
    data = nullptr;
    data = malloc(m.rows * m.step);
    memcpy(data, m.data, m.rows * m.step);
    this->tensor = MxBase::Tensor(data, std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels }, MxBase::TensorDType::UINT8);
    this->tensor.ToDevice(deviceId);
  }

  int32_t GpuMat::deviceId = -1;

  void GpuMat::setDevice(int32_t id) { deviceId = id; }

  void GpuMat::download(cv::Mat& m) {
    GpuMat gm = clone();
    gm.tensor.ToHost();
    int type = 0;
    switch (channels) {
    case 1:
      type = ACLE_8UC1;
      break;
    case 3:
      type = ACLE_8UC3;
      break;
    case 4:
      type = ACLE_8UC4;
      break;
    default:
      type = ACLE_8UC3;
      break;
    }
    m.create(rows, cols, type);
    if (!gm.tensor.GetData()) {
      E_LOG("get download data is nullptr");
      return;
    }
    m.data = (uint8_t*)gm.tensor.GetData();
  }

  void GpuMat::upload(const cv::Mat& m) {
    rows = m.rows;
    cols = m.cols;
    switch (m.type()) {
    case ACLE_8UC1:
      channels = 1;
      break;
    case ACLE_8UC3:
      channels = 3;
      break;
    case ACLE_8UC4:
      channels = 4;
      break;
    default:
      channels = 3;
      break;
    }
    step = m.step;
    if (data) free(data);
    data = malloc(rows * step);
    memcpy(data, m.data, rows * step);
    this->tensor = MxBase::Tensor(data, std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels }, MxBase::TensorDType::UINT8);
    this->tensor.ToDevice(deviceId);
  }

  GpuMat GpuMat::clone() const {
    GpuMat gm = *this;
    gm.tensor = this->tensor.Clone();
    return gm;
  }

  void GpuMat::copyTo(GpuMat& gm) {
    gm = *this;
    this->tensor.Clone(gm.tensor);
  }
}