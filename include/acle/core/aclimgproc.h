// @brief: 昇腾图像处理能力封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024. 7.24]
// @version: V0.0.1
// @revision: [Ambert@2024.7.24]
#pragma once
#include "MxBase/MxBase.h"
#include "MxBase/E2eInfer/Tensor/TensorDvpp.h"
#include "MxBase/E2eInfer/TensorOperation/TensorWarping.h"
#include "MxBase/E2eInfer/TensorOperation/TensorFusion.h"

#include "aclmat.h"

namespace acle {
	class Overlay {
	public:
		explicit Overlay(const char* func = "Default Call");
		~Overlay() = default;

		int overlayGpuAlpha(const GpuMat& src1_, const GpuMat& src2_, GpuMat& dst, int x = 0, int y = 0, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream());

		int overlayGpuRotate(const GpuMat& srcImg, GpuMat& dstImg, float angle, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream());

		void blend(const GpuMat& above_3C8U, const GpuMat& below_3C8U, const GpuMat& alphaMask_1C8U, GpuMat& dst_3C8U, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream());

		int blend2(const GpuMat& above_3C8U, GpuMat& below_3C8U, const GpuMat& alphaMask_1C8U, GpuMat& dst_3C8U, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream());

	private:
		std::vector<std::vector<float>> getRotationMatrix2D(Point2f center, float angle, double scale);

		void rotateNewSize(int& new_w, int& new_h, int old_w, int old_h, int angle);

		GpuMat div;
		GpuMat value;
		const char* function;
		bool warning = true;
		Size currentSize;
	};

	typedef std::shared_ptr<Overlay> OverlaySPtr;
	typedef std::unique_ptr<Overlay> OverlayUPtr;
}