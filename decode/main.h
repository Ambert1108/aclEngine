#pragma once
#include <iostream>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <cstring>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <map>
#include <cstdint>
#include <thread>

#include "acl/acl.h"
#include "acl/ops/acl_dvpp.h"

#include "include/seeker/common.h"
#include "include/seeker/logger.h"
#include "include/seeker/loggerApi.h"

enum Result {
  SUCCESS = 0,
  FAILED = 1
};

struct PicDesc {
  std::string picName;
  int width;
  int height;
};

aclrtRunMode runMode;