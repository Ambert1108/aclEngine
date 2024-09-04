#include "aclimgproc.h"

namespace acle {
	Overlay::Overlay(const char* func) : function(func) {}

	int Overlay::overlayGpuAlpha(const GpuMat& src1, const GpuMat& src2, GpuMat& dst, int x, int y, MxBase::AscendStream& stream) {
		if (src1.empty() || src2.empty()) {
			E_LOG("[aclimgproc::overlayGpuAlpha] input is empty");
			return -1;
		}
		int w = src1.cols;
		int h = src1.rows;

		if (w > src2.cols || h > src2.rows) {
			E_LOG("[aclimgproc::overlayGpuAlpha] input bottom src w={}, h={} > top src w={}, h={}.", 
				w, h, src2.cols, src2.rows);
			return -1;
		}
		//if (src1.channels() == 4 && w % 16 != 0) {
		//	if (warning) W_LOG("Overlay WARNING: Four-channel images width({}) need to meet 16 alignment.", w);
		//}

		if (x + w > src2.cols || y + h > src2.rows || x < 0 || y < 0) {
			W_LOG("[aclimgproc::overlayGpuAlpha] out of bound! x({})+w({}) > bottom width({}) or x({}) < 0; "
				"y({})+h({}) > bottom height({}) or y({}) < 0", x, w, src2.cols, x, y, h, src2.rows, y);
			//Todo:后续需要选取未越界部分作为叠加素材
			return -1;
		}

		if(dst.empty()) src2.copyTo(dst, stream);

		acle::Rect roi(x, y, x + w, y + h);
		acle::GpuMat roiMat = acle::GpuMat(dst, roi);

		if (src1.channels == 4 && dst.channels == 3) {
			APP_ERROR result = MxBase::BlendImages(src1.tensor, roiMat.tensor, stream);
			if (result != APP_ERR_OK) {
				E_LOG("[aclimgproc::overlayGpuAlpha] use BlendImages failed");
				return -1;
			}
			T_LOG("1");
		}
		
		else if (src1.channels == 3 && dst.channels == 4) {
			
			T_LOG("2");
		}

		else if (src1.channels == 4 && dst.channels == 4) {
			
			T_LOG("3");
		}

		else if (src1.channels == 3 && dst.channels == 3) {

			T_LOG("4");
		}

		else {
			E_LOG("Overlay error: Rendering with src1 channels={} and src2 channels={} is not supported yet.",
				src1.channels, src2.channels);
			return -1;
		}
		return 0;
	}

	int Overlay::overlayGpuRotate(const GpuMat& srcImg, GpuMat& dstImg, float angle, MxBase::AscendStream& stream) {
		if (angle < 0) { while (angle < 0) { angle = 360 + angle; } }
		else if (angle > 360) { while (angle > 360) { angle = angle - 360; } }
		if (angle == 0 || angle == 360) return 0;

		int newW = 0;
		int newH = 0;
		rotateNewSize(newW, newH, srcImg.cols, srcImg.rows, angle);

		std::vector<std::vector<float>> vec = getRotationMatrix2D(Point2f(srcImg.cols / 2, srcImg.rows / 2), angle, 1.0);
		vec.at(0).at(2) += (float)(newW - srcImg.cols) / 2;
		vec.at(1).at(2) += (float)(newH - srcImg.rows) / 2;
		dstImg = GpuMat(Size(newW, newH), ACLE_8UC4, 1);
		
		APP_ERROR result = MxBase::WarpAffineHiper(srcImg.tensor, dstImg.tensor, vec, MxBase::PaddingMode::PADDING_CONST, 255, MxBase::WarpAffineMode::INTER_LINEAR);
		if (result != APP_ERR_OK) {
			E_LOG("[aclimgproc::overlayGpuRotate] use WarpAffineHiper failed");
			return -1;
		}
		return 0;
	}

	void Overlay::blend(const GpuMat& above_3C8U, const GpuMat& below_3C8U, const GpuMat& alphaMask_1C8U, GpuMat& dst_3C8U, MxBase::AscendStream& stream) {
		if (above_3C8U.size() != below_3C8U.size()) {
			throw std::runtime_error("blend error: above_3C8U.size() != below_3C8U.size()");
		}
		if (above_3C8U.size() != alphaMask_1C8U.size()) {
			throw std::runtime_error("blend error: above_3C8U.size() != alphaMask_1C8U.size()");
		}

		//I_LOG("above size is {}x{}, current size is {}x{}", above_3C8U.size().width, above_3C8U.size().height, currentSize.width, currentSize.height);
		
		if (above_3C8U.size() != currentSize) {
			currentSize = above_3C8U.size();
			int w = currentSize.width;
			int h = currentSize.height;
			div = GpuMat(Size(w, h), ACLE_8UC1, false, MxBase::TensorDType::FLOAT16);
			div.tensor.SetTensorValue(255.0f, true);
			value = GpuMat(Size(w, h), ACLE_8UC3, false, MxBase::TensorDType::FLOAT16);
		}
		dst_3C8U = GpuMat(above_3C8U);

		MxBase::Tensor mask16F;
		MxBase::Tensor video16F;
		MxBase::Tensor bg16F;
		MxBase::Tensor maskDivDst;

		D_LOG("replace --- 0 ---");
		MxBase::ConvertTo(alphaMask_1C8U.tensor, mask16F, MxBase::TensorDType::FLOAT16);
		D_LOG("replace --- 1 ---");
		MxBase::Divide(mask16F, div.tensor, maskDivDst);
		D_LOG("replace --- 2 ---");
		std::vector<MxBase::Tensor> tv{ maskDivDst.Clone(), maskDivDst.Clone(), maskDivDst.Clone() };
		MxBase::Tensor mask;
		MxBase::Merge(tv, mask);
		D_LOG("replace --- 3 ---");
		MxBase::ConvertTo(above_3C8U.tensor, video16F, MxBase::TensorDType::FLOAT16);
		D_LOG("replace --- 4 ---");
		MxBase::ConvertTo(below_3C8U.tensor, bg16F, MxBase::TensorDType::FLOAT16);
		D_LOG("replace --- 5 ---");
		MxBase::Tensor videoMulDst;
		MxBase::Multiply(video16F, mask, videoMulDst);
		D_LOG("replace --- 6 ---");
		value.tensor.SetTensorValue(-1.0f, true);
		MxBase::Tensor maskMulDst1, maskMulDst;
		MxBase::Multiply(mask, value.tensor, maskMulDst1);
		D_LOG("replace --- 7 ---");
		value.tensor.SetTensorValue(1.0f, true);
		MxBase::Add(maskMulDst1, value.tensor, maskMulDst);
		D_LOG("replace --- 8 ---");
		MxBase::Tensor bgMulDst;
		MxBase::Multiply(bg16F, maskMulDst, bgMulDst);
		D_LOG("replace --- 9 ---");
		MxBase::Tensor addDst;
		MxBase::Add(videoMulDst, bgMulDst, addDst);
		D_LOG("replace --- 10 ---");
		MxBase::ConvertTo(addDst, dst_3C8U.tensor, MxBase::TensorDType::UINT8);
		D_LOG("replace --- 11 ---");
	}

	int Overlay::blend2(const GpuMat& above_3C8U, GpuMat& below_3C8U, const GpuMat& alphaMask_1C8U, GpuMat& dst_3C8U, MxBase::AscendStream& stream) {
		if (above_3C8U.size() != below_3C8U.size()) {
			E_LOG("[aclimgproc::blend] above_3C8U.size() != below_3C8U.size()");
			return -1;
		}
		if (above_3C8U.size() != alphaMask_1C8U.size()) {
			E_LOG("[aclimgproc::blend] above_3C8U.size() != alphaMask_1C8U.size()");
			return -1;
		}
		dst_3C8U = GpuMat(above_3C8U);

		MxBase::Tensor maskTmpTensor, maskTensor;
		APP_ERROR result = APP_ERR_OK;
		result = MxBase::ConvertTo(alphaMask_1C8U.tensor, maskTmpTensor, MxBase::TensorDType::FLOAT16);
		if (result != APP_ERR_OK) {
			E_LOG("[aclimgproc::blend] convert mask tensor to FLOAT16 failed");
			return -1;
		}

		//mask二值化避免花边
		result = MxBase::ThresholdBinary(maskTmpTensor, maskTensor, 125, 1);
		if (result != APP_ERR_OK) {
			E_LOG("[aclimgproc::blend] ThresholdBinary mask tensor failed");
			return -1;
		}

		result = MxBase::BackgroundReplace(below_3C8U.tensor, above_3C8U.tensor, maskTensor, dst_3C8U.tensor);
		if (result != APP_ERR_OK) {
			E_LOG("[aclimgproc::blend] use BackgroundReplace failed");
			return -1;
		}
		I_LOG("bg replace success");
		return 0;
	}

	int Overlay::overlayGpuScale(const GpuMat& srcImg, GpuMat& dstImg, int w, int h) {
		if (srcImg.empty()) {
			E_LOG("[aclimgproc::overlayGpuScale] source Image data is empty.");
			return -1;
		}
		if (w <= 0 || h <= 0) {
			E_LOG("[aclimgproc::overlayGpuScale] input width {}, height {} is error.", w, h);
			return -1;
		}

		if (w * h == srcImg.cols * srcImg.rows && w == srcImg.cols && h == srcImg.rows) {
			E_LOG("[aclimgproc::overlayGpuScale] input w {} and h {} == src w {} and h {}",
				w, h, srcImg.cols, srcImg.rows);
			return -1;
		}
		else {
			//dstImg.cols = srcImg.cols;
			//dstImg.rows = srcImg.rows;
			//dstImg.channels = srcImg.channels;
			//dstImg.step = srcImg.step;
			//dstImg.matSize = srcImg.matSize;
			//MxBase::Tensor src, dst;
			//MxBase::CvtColor(srcImg.tensor, src, MxBase::CvtColorMode::COLOR_RGBA2RGB);
			//MxBase::Resize(src, dst, MxBase::Size(w, h));
			//MxBase::CvtColor(dst, dstImg.tensor, MxBase::CvtColorMode::COLOR_RGB2RGBA);
			//暂时不支持缩放rgba格式，调试中
			srcImg.copyTo(dstImg);
		}
		return 0;
	}

	std::vector<std::vector<float>> Overlay::getRotationMatrix2D(Point2f center, float angle, double scale) {
		float angleRad = angle * M_PI / 180.0;
		float alpha = cos(angleRad);
		float beta = sin(angleRad);
		
		std::vector<std::vector<float>> rotationMatrix = {
				{alpha, beta, (1 - alpha) * center.x - beta * center.y},
				{-beta, alpha, beta * center.x + (1 - alpha) * center.y}
		};
		
		return rotationMatrix;
	}
		
	void Overlay::rotateNewSize(int& new_w, int& new_h, int old_w, int old_h, int angle) {
		new_w = fabs(sin(M_PI * (double)(angle / 180.0))) * old_h + fabs(cos(M_PI * (double)(angle / 180.0))) * old_w;
		new_h = fabs(sin(M_PI * (double)(angle / 180.0))) * old_w + fabs(cos(M_PI * (double)(angle / 180.0))) * old_h;
	}
}
