#include "aclimgproc.h"

namespace acle {
	int overlayGpuAlpha(const GpuMat& src1, const GpuMat& src2, GpuMat& dst, int x, int y, MxBase::AscendStream& stream) {
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
			E_LOG("[aclimgproc::overlayGpuAlpha] out of bound! x({})+w({}) > bottom width({}) or x({}) < 0; "
				"y({})+h({}) > bottom height({}) or y({}) < 0", x, w, src2.cols, x, y, h, src2.rows, y);
			return -1;
		}

		src2.copyTo(dst, stream);
		//stream.Synchronize();

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
		
		else if (src1.channels == 3 && src2.channels == 4) {
			
			T_LOG("2");
		}

		else if (src1.channels == 4 && src2.channels == 4) {
			
			T_LOG("3");
		}

		else if (src1.channels == 3 && src2.channels == 3) {

			T_LOG("4");
		}

		else {
			E_LOG("Overlay error: Rendering with src1 channels={} and src2 channels={} is not supported yet.",
				src1.channels, src2.channels);
			return -1;
		}
		return 0;
	}

	int overlayGpuRotate(const GpuMat& srcImg, GpuMat& dstImg, float angle, MxBase::AscendStream& stream) {
		if (angle < 0) { while (angle < 0) { angle = 360 + angle; } }
		else if (angle > 360) { while (angle > 360) { angle = angle - 360; } }
		if (angle == 0 || angle == 360) return 0;

		using namespace own;
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

	namespace own {
		std::vector<std::vector<float>> getRotationMatrix2D(Point2f center, float angle, double scale) {
			float angleRad = angle * M_PI / 180.0;
			float alpha = cos(angleRad);
			float beta = sin(angleRad);
		
			std::vector<std::vector<float>> rotationMatrix = {
					{alpha, beta, (1 - alpha) * center.x - beta * center.y},
					{-beta, alpha, beta * center.x + (1 - alpha) * center.y}
			};
		
			return rotationMatrix;
		}
		
		void rotateNewSize(int& new_w, int& new_h, int old_w, int old_h, int angle) {
			new_w = fabs(sin(M_PI * (double)(angle / 180.0))) * old_h + fabs(cos(M_PI * (double)(angle / 180.0))) * old_w;
			new_h = fabs(sin(M_PI * (double)(angle / 180.0))) * old_w + fabs(cos(M_PI * (double)(angle / 180.0))) * old_h;
		}
	}
}
