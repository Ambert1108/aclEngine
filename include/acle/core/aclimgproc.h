// @brief: 昇腾图像处理能力封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024. 7.24]
// @version: V0.0.1
// @revision: [Ambert@2024.7.24]
#pragma once
#include "MxBase/E2eInfer/Tensor/TensorDvpp.h"
#include "MxBase/E2eInfer/TensorOperation/TensorWarping.h"
#include "MxBase/E2eInfer/TensorOperation/TensorFusion.h"

#include "aclmat.h"

namespace acle {
	int overlayGpuAlpha(const GpuMat& src1_, const GpuMat& src2_, GpuMat& dst, int x = 0, int y = 0, MxBase::AscendStream& stream = MxBase::AscendStream::DefaultStream());

	int overlayGpuRotate(const GpuMat& srcImg, GpuMat& dstImg, float angle);
}