#pragma once
#include "AclLiteImageProc.h"
#include "acllite/AclLiteError.h"

namespace huawei {

	class VideoTool {
	public:
		VideoTool() {};
		~VideoTool() { close(); };

		AclLiteError open() {
			AclLiteError ret = imageProc.Init();
			if (ret) {
				ACLLITE_LOG_ERROR("Tool::open::Error:imageProc init failed, error %d", ret);
				return ACLLITE_ERROR;
			}

			return ACLLITE_OK;
		}

		AclLiteError resize(ImageData& dest, ImageData& src, uint32_t width, uint32_t height) {
			AclLiteError ret = imageProc.Resize(dest, src, width, height);
			if (ret) {
				ACLLITE_LOG_ERROR("Tool::resize::Error:resize size:%dx%d failed, error %d", width, height, ret);
				return ACLLITE_ERROR;
			}

			return ACLLITE_OK;
		}

	private:
		void close() { imageProc.DestroyResource(); }

		AclLiteImageProc imageProc;
	};

}