#include "aclengine/AclEngine.hpp"
#include "VideoEngine23.hpp"

int main() {
  using namespace acle;
  I_LOG("test decoder death handle start");
  AclLiteResource aclDev(1, "");
  if (aclDev.Init() != ACLLITE_OK) {
    I_LOG("init acl dev failed");
    return -1;
  }
  std::string inputName = "/home/data/v2.mp4";
  acle::Demuxer* demuxer = new acle::Demuxer();
  if (!demuxer->open(inputName)) {
    return -1;
  }

  acle::CodecFormat fmt;
  fmt.width = 1280;
  fmt.height = 720;
  acle::Decoder* decoder = new acle::Decoder(fmt);
  decoder->open();
  AclPacket rpkt;
  demuxer->demux(rpkt);
  AclFrame frame;
  AclLiteError ret = decoder->readFrame(rpkt, frame);
  decoder->close();
  delete decoder;
  I_LOG("test decoder death handle finish");
	return 0;
}