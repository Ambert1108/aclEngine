# acllink用于将aclengine实现的源码编译到指定程序中
# 使用者在需要使用aclengine时引入此文件并将ACLE加入到add_executable中即可

set(ACLE_DIR "${PROJECT_SOURCE_DIR}/include/acle/core")
aux_source_directory(${ACLE_DIR} ACLE)