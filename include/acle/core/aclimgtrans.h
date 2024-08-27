// @brief: 昇腾图像数据转换封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.8.22]
// @version: V0.0.1
// @revision: [Ambert@2024.8.22]
#pragma once

#include <MxBase/E2eInfer/GlobalInit/GlobalInit.h>
#include "MxBase/E2eInfer/Image/Image.h"
#include "MxBase/E2eInfer/ImageProcessor/ImageProcessor.h"
#include "MxBase/E2eInfer/Tensor/TensorDvpp.h"

extern "C" {
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
}

#include "utils.h"
#include "aclmat.h"

namespace acle {
	class Transfer {
	public:
		Transfer(const char* func = "Default Call");

		~Transfer();

		void enableHWDevice(AVBufferRef* buf);

		int gpu_transfer_frame_to_mat(const AVFrame* src_frame, GpuMat& dstMat, int32_t w = 0, int64_t h = 0);

		int gpu_transfer_mat_to_frame(GpuMat src_mat, AVFrame*& cuda_frame, AVPixelFormat target_format);

	private:
		const char* function = nullptr;
		AVFrame* outFrame = nullptr;
		AVBufferRef* hwCtx = nullptr;
		std::shared_ptr<uint8_t> data = nullptr;
	};

	typedef std::shared_ptr<Transfer> TransferSPtr;
	typedef std::unique_ptr<Transfer> TransferUPtr;
}