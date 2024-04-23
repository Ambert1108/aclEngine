#include "main.h"

std::string filePath;
int inputWidth;
int inputHeight;

int32_t deviceId;
aclrtContext context_;
aclrtStream stream_;
pthread_t threadId_;

int32_t format_ = 1; // 1：YUV420 semi-planner（nv12）; 2：YVU420 semi-planner（nv21）

/* 0：H265 main level
 * 1：H264 baseline level
 * 2：H264 main level
 * 3：H264 high level
 */
int32_t enType_ = 1;

aclvdecChannelDesc* vdecChannelDesc_;
acldvppStreamDesc* streamInputDesc_;
acldvppPicDesc* picOutputDesc_;
void* picOutBufferDev;
static bool runFlag = true;

void* threadFunc(aclrtContext context) {
	if (context == nullptr) {
		E_LOG("context can not be nullptr");
		return reinterpret_cast<void*>(-1);
	}

	I_LOG("use context for this thread");
	aclError ret = aclrtSetCurrentContext(context);
	if (ret != ACL_SUCCESS) {
		E_LOG("aclSetCurrentContext failed, errCode={}", static_cast<int32_t>(ret));
		return reinterpret_cast<void*>(-1);
	}

	I_LOG("process callback thread start");
	while (runFlag) {
		aclError aclRet = aclrtProcessReport(1000);
		//W_LOG("wait, runFlag={}", runFlag);
	}
	I_LOG("process callback thread stop");
	
	return nullptr;
}

bool ReadFileToDeviceMem(const char* fileName, void*& dataDev, uint32_t& dataSize) {
	FILE* fp = fopen(fileName, "rb+");

	//移动文件指针至文件末尾
	fseek(fp, 0, SEEK_END);
	//计算文件长度
	long fileLenLong = ftell(fp);
	//将文件指针移回文件起始位置
	fseek(fp, 0, SEEK_SET);

	auto fileLen = static_cast<uint32_t>(fileLenLong);

	void* data = malloc(fileLen);
	size_t readSize = fread(data, 1, fileLen, fp);
	if (readSize < fileLen) {
		free(data);
		fclose(fp);
		return false;
	}

	dataSize = fileLen;
	auto aclRet = acldvppMalloc(&dataDev, dataSize);
	if (runMode == ACL_HOST) {
		aclRet = aclrtMemcpy(dataDev, dataSize, data, fileLen, ACL_MEMCPY_HOST_TO_DEVICE);
	}
	else {
		aclRet = aclrtMemcpy(dataDev, dataSize, data, fileLen, ACL_MEMCPY_DEVICE_TO_DEVICE);
	}

	free(data);
	fclose(fp);
	return true;
}

bool writeToFile(const char* fileName, const void* dataDev, uint32_t dataSize) {
	if (dataSize <= 0) {
		E_LOG("dataSize value is abnormal. dataSize={}", dataSize);
		return false;
	}
	void* data = malloc(dataSize);
	if (data == nullptr) {
		E_LOG("malloc data buffer failed. dataSize={}", dataSize);
		return false;
	}

	if (runMode == ACL_HOST) {
		auto aclRet = aclrtMemcpy(data, dataSize, dataDev, dataSize, ACL_MEMCPY_DEVICE_TO_HOST);
	}
	else {
		auto aclRet = aclrtMemcpy(data, dataSize, dataDev, dataSize, ACL_MEMCPY_DEVICE_TO_DEVICE);
	}

	FILE* outFileFp = fopen(fileName, "wb+");

	bool ret = true;
	size_t writeRet = fwrite(data, 1, dataSize, outFileFp);
	if (writeRet != dataSize) {
		ret = false;
	}
	I_LOG("write to {}", fileName);
	free(data);
	fflush(outFileFp);
	fclose(outFileFp);
	return ret;
}

void callback(acldvppStreamDesc* input, acldvppPicDesc* output, void* userdata) {
	void* vdecOutBuffDev = acldvppGetPicDescData(output);
	uint32_t size = acldvppGetPicDescSize(output);
	static int count = 1;
	std::string fileSaveName = "./output/image" + std::to_string(count) + ".yuv";
	if (!writeToFile(fileSaveName.c_str(), vdecOutBuffDev, size)) {
		E_LOG("write file failed, count={}", count);
	}

	aclError ret = acldvppFree(reinterpret_cast<void*>(vdecOutBuffDev));
	ret = acldvppDestroyPicDesc(output);

	count++;
}

void destroyResource() {
	aclrtDestroyStream(stream_);
	stream_ = nullptr;
	aclrtDestroyContext(context_);
	context_ = nullptr;
	aclFinalize();
}

int main(int argc, char* argv[]) {

	if (argc < 4) {
		E_LOG("Please input: ./main <inputVideoFilePath> <inputVideoWidth> <inputVideoHeight> <deviceId>");
		return -1;
	}

	filePath = argv[1];
	inputWidth = std::atoi(argv[2]);
	inputHeight = std::atoi(argv[3]);
	deviceId = std::atoi(argv[4]);

	I_LOG("filePath={}\ninputWidth={}\ninputHeight={}\ndeviceId={}",
		filePath, inputWidth, inputHeight, deviceId);

	
	aclError ret = aclInit(NULL);
	ret = aclrtSetDevice(deviceId);
	ret = aclrtCreateContext(&context_, deviceId);
	ret = aclrtCreateStream(&stream_);
	aclrtGetRunMode(&runMode);

	DIR* dir;
	if ((dir = opendir("./output")) == NULL)
		system("mkdir ./output");


	//std::thread th(threadFunc, std::ref(context_));
	pthread_create(&threadId_, nullptr, threadFunc, context_);

	vdecChannelDesc_ = aclvdecCreateChannelDesc();

	int channelId = 10;
	//std::thread::id thread_id = th.get_id();
	//std::stringstream ss;
	//ss << thread_id;
	//std::string mystring = ss.str();
	//mystring = mystring.substr(0, 5);
	I_LOG("threadId={}", threadId_);
	//return 0;
	//uint64_t threadId = std::stoi(mystring);

	ret = aclvdecSetChannelDescChannelId(vdecChannelDesc_, channelId);
	ret = aclvdecSetChannelDescThreadId(vdecChannelDesc_, threadId_);
	ret = aclvdecSetChannelDescCallback(vdecChannelDesc_, callback);

	ret = aclvdecSetChannelDescEnType(vdecChannelDesc_, static_cast<acldvppStreamFormat>(enType_));

	ret = aclvdecSetChannelDescOutPicFormat(vdecChannelDesc_, static_cast<acldvppPixelFormat>(format_));

	ret = aclvdecCreateChannel(vdecChannelDesc_);

	int restLen = 10;
	void* inBufferDev = nullptr;
	uint32_t inBufferSize = 0;
	size_t dataSize = (inputWidth * inputHeight * 3) / 2;

	ReadFileToDeviceMem(filePath.c_str(), inBufferDev, inBufferSize);

	streamInputDesc_ = acldvppCreateStreamDesc();
	while (restLen > 0) {
		ret = acldvppSetStreamDescData(streamInputDesc_, inBufferDev);
		ret = acldvppSetStreamDescSize(streamInputDesc_, inBufferSize);
		ret = acldvppMalloc(&picOutBufferDev, dataSize);

		picOutputDesc_ = acldvppCreatePicDesc();
		ret = acldvppSetPicDescData(picOutputDesc_, picOutBufferDev);
		ret = acldvppSetPicDescSize(picOutputDesc_, dataSize);
		ret = acldvppSetPicDescFormat(picOutputDesc_, static_cast<acldvppPixelFormat>(format_));

		ret = aclvdecSendFrame(vdecChannelDesc_, streamInputDesc_, picOutputDesc_, nullptr, nullptr);

		restLen -= 1;
		I_LOG("remaining {} frame", restLen);
	}
	I_LOG("stop main loop");
	ret = acldvppDestroyStreamDesc(streamInputDesc_);
	ret = aclvdecDestroyChannel(vdecChannelDesc_);
	aclvdecDestroyChannelDesc(vdecChannelDesc_);
	vdecChannelDesc_ = nullptr;
	I_LOG("destory vdec channel");

	runFlag = false;
	//if(th.joinable()) th.join();
	void* res = nullptr;
	pthread_join(threadId_, &res);
	acldvppFree(inBufferDev);

	destroyResource();
	return SUCCESS;
}