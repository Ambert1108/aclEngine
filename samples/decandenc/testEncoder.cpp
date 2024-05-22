#include "aclengine/AclEngine.hpp"

int main() {
  I_LOG("test encoder death handle start");
  AclLiteResource aclDev(1, "");
  if (aclDev.Init() != ACLLITE_OK) {
    I_LOG("init acl dev failed");
    return -1;
  }

  acle::CodecFormat fmt;
  fmt.width = 1280;
  fmt.height = 720;
  aclrtGetCurrentContext(&fmt.context);
  acle::Encoder* encoder = new acle::Encoder(fmt);
  encoder->open();
  encoder->close();
  delete encoder;
  I_LOG("test encoder death handle finish");
	return 0;
}