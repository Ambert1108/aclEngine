#include "aclmat.h"

namespace acle {
	template<typename _T> inline Point__<_T>::Point__() : x(0), y(0) {};
	template<typename _T> inline Point__<_T>::Point__(_T _x, _T _y) : x(_x), y(_y) {};
	template class Point__<int>;
	template class Point__<int64_t>;
	template class Point__<float>;
	template class Point__<double>;

	template<typename _T> inline Size__<_T>::Size__() : width(0), height(0) {};
	template<typename _T> inline Size__<_T>::Size__(_T _width, _T _height) : width(_width), height(_height) {};
	template<typename _T> inline Size__<_T>::Size__(const Size__<_T>& size) : width(size.width), height(size.height) {};
	template<typename _T> inline Size__<_T>::Size__(const Point__<_T>& pt) : width(pt.x), height(pt.y) {};
	template<typename _T> inline bool Size__<_T>::empty() const { return width <= 0 || height <= 0; }
	template class Size__<int>;
	template class Size__<int64_t>;
	template class Size__<float>;
	template class Size__<double>;

	GpuMat::GpuMat() : rows(0.0), cols(0.0), channels(0), step(0), data(nullptr) {};

	GpuMat::~GpuMat() {
		release();
	};

	GpuMat::GpuMat(const GpuMat& gm) {
		this->rows = gm.rows;
		this->cols = gm.cols;
		this->channels = gm.channels;
		this->step = gm.step;
		this->matSize = gm.size();
		this->tensor = gm.tensor;
		data = nullptr;
	}

	GpuMat::GpuMat(const GpuMat& gm, const Rect& rect) {
		this->rows = rect.y1 - rect.y0;
		this->cols = rect.x1 - rect.x0;
		this->channels = gm.channels;
		this->step = (size_t)this->cols * this->channels;
		this->matSize = gm.size();
		this->tensor = MxBase::Tensor(gm.tensor, rect);
		data = nullptr;
	}

	GpuMat& GpuMat::operator=(const GpuMat & gm) {
		this->rows = gm.rows;
		this->cols = gm.cols;
		this->channels = gm.channels;
		this->step = gm.step;
		this->matSize = gm.matSize;
		this->tensor = gm.tensor;
		return *this;
	}

	bool GpuMat::operator==(const GpuMat& gm) {
		return this->tensor == gm.tensor;
	}

	GpuMat::GpuMat(int rows, int cols, int type, bool flag, MxBase::TensorDType dataType) {
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
		this->step = (size_t)this->cols * this->channels;
		this->matSize.width = cols;
		this->matSize.height = rows;
		size_t size = (size_t)this->rows * this->step;
		data = nullptr;
		switch (dataType) {
		case MxBase::TensorDType::UNDEFINED:
			break;
		case MxBase::TensorDType::FLOAT32:
		case MxBase::TensorDType::FLOAT16:
		case MxBase::TensorDType::DOUBLE64:
			data = (float*)malloc(size * sizeof(float));
			break;
		case MxBase::TensorDType::INT8:
		case MxBase::TensorDType::INT32:
		case MxBase::TensorDType::UINT8:
		case MxBase::TensorDType::INT16:
		case MxBase::TensorDType::UINT16:
		case MxBase::TensorDType::UINT32:
		case MxBase::TensorDType::INT64:
		case MxBase::TensorDType::UINT64:
			data = malloc(size);
			break;
		default:
			break;
		}
		memset(data, 0, size);
		
		if (flag) {
			// use NHWC
			this->tensor = MxBase::Tensor(data, std::vector<uint32_t>{ 1, (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels }, dataType);
		}
		else {
			// use HWC
			this->tensor = MxBase::Tensor(data, std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels }, dataType);
		}
		this->tensor.ToDevice(acl::deviceId);
	}

	GpuMat::GpuMat(Size size, int type, bool flag, MxBase::TensorDType dataType) 
		: GpuMat(size.height, size.width, type, flag, dataType) {}

	GpuMat::GpuMat(const cv::Mat& m, bool flag, MxBase::TensorDType dataType) : GpuMat() {
		upload(m, flag, dataType);
	}

	GpuMat::GpuMat(int rows, int cols, int type, void* data, size_t dataSize) {
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
		this->step = step;
		this->matSize.width = cols;
		this->matSize.height = rows;
		//size_t size = (size_t)this->rows * this->step;
		this->data = nullptr;
		this->data = malloc(dataSize);
		memcpy(this->data, data, dataSize);
		this->tensor = MxBase::Tensor(data, std::vector<uint32_t>{ (uint32_t)this->rows, (uint32_t)this->cols, (uint32_t)this->channels }, MxBase::TensorDType::UINT8, acl::deviceId);
	}

	GpuMat::GpuMat(Size size, int type, void* data, size_t dataSize) : GpuMat(size.height, size.width, type, data, dataSize) {}

	void GpuMat::download(cv::Mat& m, MxBase::AscendStream& stream) {
		if (empty()) return;
		GpuMat gm = clone(stream);
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
		stream.Synchronize();
		gm.tensor.ToHost();
		if (!gm.tensor.GetData()) {
			E_LOG("get download data is nullptr");
			return;
		}
		m.create(gm.rows, gm.cols, type);
		m.data = (uint8_t*)gm.tensor.GetData();
		if (m.empty()) {
			E_LOG("download data failed, mat is empty");
			return;
		}
	}

	void GpuMat::upload(const cv::Mat& m, bool flag, MxBase::TensorDType dataType) {
		if (m.empty()) return;
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
		matSize.width = cols;
		matSize.height = rows;
		if (data) free(data);
		data = malloc(rows * step);
		memcpy(data, m.data, rows * step);
		if (flag) {
			// use NHWC
			tensor = MxBase::Tensor(data, std::vector<uint32_t>{ 1, (uint32_t)rows, (uint32_t)cols, (uint32_t)channels }, dataType);
		}
		else {
			// use HWC
			tensor = MxBase::Tensor(data, std::vector<uint32_t>{ (uint32_t)rows, (uint32_t)cols, (uint32_t)channels }, dataType);
		}
		tensor.ToDevice(acl::deviceId);
	}

	GpuMat GpuMat::clone(MxBase::AscendStream& stream) const {
		GpuMat gm = *this;
		gm.tensor = tensor.Clone(stream);
		return gm;
	}

	void GpuMat::copyTo(GpuMat& gm, MxBase::AscendStream& stream) const {
		gm = clone(stream);
	}

	bool GpuMat::empty() const {
		return tensor.IsEmpty() && !data;
	}

	Size GpuMat::size() const {
		return matSize;
	}

	void GpuMat::release() {
		if (data) {
			free(data);
			data = nullptr;
		}
	}
}