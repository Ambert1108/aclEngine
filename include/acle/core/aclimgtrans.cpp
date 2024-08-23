#include "aclimgtrans.h"

namespace acle {
	static AVBufferRef* createHwFrameCtx(AVBufferRef* hw_device_ctx, AVPixelFormat fmt,
		AVPixelFormat swFmt, int width, int height, int poolSize = 0) {
		AVBufferRef* hw_frames_ref;
		AVHWFramesContext* frames_ctx = nullptr;

		int err = 0;

		if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx))) {
			std::cerr << "Gpu error->[createHwFrameCtx] Failed to create hw frame context." << std::endl;
			return nullptr;
		}

		frames_ctx = (AVHWFramesContext*)(hw_frames_ref->data);
		frames_ctx->format = fmt;
		frames_ctx->sw_format = swFmt;
		frames_ctx->width = width;
		frames_ctx->height = height;
		frames_ctx->initial_pool_size = poolSize;
		if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
			std::cerr << "Gpu error->[createHwFrameCtx] Failed to initialize hardware frame context." << std::endl;
			av_buffer_unref(&hw_frames_ref);
			return nullptr;
		}

		return hw_frames_ref;
	}

	static void createHwDeviceCtx(AVBufferRef*& hw_device_ctx, const char* deviceID, AVHWDeviceType type = AV_HWDEVICE_TYPE_CUDA) {
		if (hw_device_ctx != nullptr)
			return;
		if (av_hwdevice_ctx_create(&hw_device_ctx, type, deviceID, NULL, 0) < 0)
			throw std::runtime_error(": Failed to create specified HW device:" + *deviceID);
	}

	static inline void checkPixFmtOpencv(int& format) {
		//若格式不为BGR24且不为BGRA
		if (format == 3 || format == 28) return;
		//若格式不为RGBA且不为YUVA420P
		if (format != 26 && format != 33)
			format = 3; //设格式为BGR24
		else
			format = 28; //设格式为BGRA
	}

	static int checkHwFrameSwFormat(const AVFrame* F) {
		AVHWFramesContext* frames_ctx = (AVHWFramesContext*)(F->hw_frames_ctx->data);
		return frames_ctx->sw_format;
	}

	Transfer::Transfer(const char* func) : function(func) {}

	Transfer::~Transfer() {
		if (outFrame) {
			av_frame_free(&outFrame);
			outFrame = nullptr;
		}
		if (hwCtx) {
			av_buffer_unref(&hwCtx);
			hwCtx = nullptr;
		}
	}

	void Transfer::enableHWDevice(AVBufferRef* buf) {
		if (!buf) {
			E_LOG("[aclimgtrans::enableHWDevice] hwCtx is nullptr!");
			throw std::invalid_argument("[aclimgtrans::enableHWDevice] hwCtx is nullptr!");
		}
		hwCtx = av_buffer_ref(buf);
	}

	int Transfer::gpu_transfer_frame_to_mat(const AVFrame* src, GpuMat& dst, int32_t w, int64_t h) {
		if (src->format != 119 || checkHwFrameSwFormat(src) != 23) {
			W_LOG("[aclimgtrans::frame_to_mat] src frame format should be ascend(nv12)");
			return -2;
		}

		if (w == 0) w = src->width;
		if (h == 0) h = src->height;

		uint8_t* yPlane = src->data[0]; // Y 平面指针
		uint8_t* uvPlane = src->data[1]; // UV 平面指针

		// 计算总数据大小
		size_t totalSize = src->linesize[0] * h + src->linesize[1] * (h / 2);
		if (data) data.reset();
		void* raw = copyData(src->data[0], totalSize, aclrtMemcpyKind::ACL_MEMCPY_DEVICE_TO_DEVICE, MemoryType::DVPP);
		data = SHARED_PTR_DVPP_BUF(raw);
		MxBase::Image image(data, totalSize, acl::deviceId, MxBase::Size(w, h));
		MxBase::Tensor tensor(std::vector<uint32_t>{(uint32_t)h, (uint32_t)w, 3}, MxBase::TensorDType::UINT8, acl::deviceId);
		MxBase::Tensor::TensorMalloc(tensor);
		tensor = image.ConvertToTensor();
		dst = GpuMat(h, w, ACLE_8UC3);
		try { dst.tensor = image.ConvertToTensor(false, false); }
		catch (std::exception& ex) {
			E_LOG("[aclimgtrans::frame_to_mat] catch exception:{}", ex.what());
			return -1;
		}

		if (dst.tensor.IsEmpty()) {
			W_LOG("[aclimgtrans::frame_to_mat] convert frame to mat failed");
			return -1;
		}
		return 0;
	}

	int Transfer::gpu_transfer_mat_to_frame(GpuMat src_mat, AVFrame*& cuda_frame, AVPixelFormat target_format) {
		if (cuda_frame->data == nullptr) {
			cuda_frame = av_frame_alloc();
		}
		int w = src_mat.cols;
		int h = src_mat.rows;

		//target_format == AVPixelFormat::AV_PIX_FMT_YUV420P)
		if (src_mat.empty()) {
			E_LOG("Transfer error: src mat is empty");
			return -1;
		}
		if (src_mat.channels != 3) {
			E_LOG("Transfer error: transfer mat channels is not 3");
			return -1;
		}
		if (outFrame == nullptr || outFrame->width != src_mat.cols || outFrame->height != src_mat.rows) {
			if (outFrame) {
				av_frame_free(&outFrame);
				outFrame = nullptr;
			}
			AVBufferRef* hwFramesCtx = createHwFrameCtx(hwCtx, AV_PIX_FMT_ASCEND, AV_PIX_FMT_YUV420P, w, h);
			outFrame = av_frame_alloc();
			int err = av_hwframe_get_buffer(hwFramesCtx, outFrame, 0);
			if (err != 0)
				throw std::runtime_error("error: av_hwframe_get_buffer failed.");
			av_buffer_unref(&hwFramesCtx);
		}
		uint8_t* Data[3] = { outFrame->data[0], outFrame->data[1], outFrame->data[2] };
		int Linesize[3] = { outFrame->linesize[0], outFrame->linesize[1], outFrame->linesize[2] };
		//nppCtx->RGB_TO_YUV420P(src_mat, Data, Linesize);
		av_frame_ref(cuda_frame, outFrame);
		return 0;
	}
}