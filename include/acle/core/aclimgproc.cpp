#include "aclimgproc.h"

namespace acle {
	int overlayGpuAlpha(const GpuMat& src1, const GpuMat& src2, GpuMat& dst, int x, int y) {
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

		src2.copyTo(dst);

		acle::Rect roi(x, y, x + w, y + h);
		acle::GpuMat roiMat = acle::GpuMat(dst, roi);

		if (src1.channels == 4 && dst.channels == 3) {
			APP_ERROR result = MxBase::BlendImages(src1.tensor, roiMat.tensor);
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
			
		}

		else {
			E_LOG("Overlay error: Rendering with src1 channels={} and src2 channels={} is not supported yet.",
				src1.channels, src2.channels);
			return -1;
		}
		return 0;
	}

	int overlayGpuRotate(const GpuMat& srcImg, GpuMat& dstImg, float angle) {
		//cv::cuda::Stream stream = cv::cuda::Stream::Null()) {
		//cv::Mat rm; //旋转变换矩阵
		//
		//int src_w = srcImg.cols;
		//int src_h = srcImg.rows;
		//rm = cv::getRotationMatrix2D(cv::Point2f(src_w / 2, src_h / 2), angle, 1);
		//if (rm.empty()) return -1;
		//
		//float new_w = 0.0;
		//float new_h = 0.0;
		//planning_rotateImg_new_size(new_w, new_h, src_w, src_h, angle);
		//
		//T_LOG("new width={}, height={}", new_w, new_h);
		//rm.at<double>(0, 2) += (new_w - src_w) / 2;
		//rm.at<double>(1, 2) += (new_h - src_h) / 2;
		//
		//cv::cuda::warpAffine(srcImg, dstImg, rm, cv::Size(new_w, new_h),
		//	cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(), stream);
			return 0;
	}
}