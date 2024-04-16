cmake_minimum_required(VERSION 3.5.1)

# project information
project(venc)

# Compile options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#add_compile_options(-std=c++11)

add_definitions(-DENABLE_DVPP_INTERFACE)

# Set up environment
set(Euler open)

# Specify target generation path
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  "out")
set(CMAKE_CXX_FLAGS_DEBUG "-fPIC -O0 -g -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-fPIC -O2 -Wall")

if(${Euler} MATCHES "open")
    set(ENV{INSTALL_DIR} /usr/local/Ascend/ascend-toolkit/latest)
    set(ENV{CPU_ARCH} aarch64)
    set(ENV{THIRDPART_PATH} /usr/local/Ascend/thirdpart/$ENV{CPU_ARCH})
    message("Run Environment: openEuler")
else()
    #set(ENV{INSTALL_DIR} /usr/local/Ascend/ascend-toolkit/latest)
    #set(ENV{CPU_ARCH} aarch64)
    #set(ENV{THIRDPART_PATH} /tmp/Ascend/thirdpart/$ENV{CPU_ARCH})
    message("Run Environment: other")
endif()


if (NOT DEFINED ENV{INSTALL_DIR})
    message(FATAL_ERROR "Not Defined INSTALL_DIR")
else()
    message("INSTALL_DIR: $ENV{INSTALL_DIR}")
endif()

if (NOT DEFINED ENV{THIRDPART_PATH})
    message(FATAL_ERROR "Not Defined THIRDPART_PATH")
else()
    message("THIRDPART_PATH: $ENV{THIRDPART_PATH}")
endif()

if (NOT DEFINED ENV{CPU_ARCH})
    message(FATAL_ERROR "Not Defined CPU_ARCH")
else()
    message("CPU_ARCH: $ENV{CPU_ARCH}")
endif()

message("arm architecture detected")
if(EXISTS "$ENV{INSTALL_DIR}/driver/libmedia_mini.so")
    message("target aarch64 200dk")
else()
    message("target x86 300")
endif()

# Header path
include_directories(
    $ENV{INSTALL_DIR}/runtime/include/
    ${THIRDPART_PATH}/include/
)

# add host lib path
link_directories(
    $ENV{INSTALL_DIR}/runtime/lib64/stub
    ${THIRDPART_PATH}/lib/
)

add_executable(main
        main.cpp)

#target_link_libraries(main
#        ascendcl acl_dvpp pthread stdc++)
target_link_libraries(main ascendcl acl_dvpp stdc++ pthread opencv_highgui opencv_core opencv_imgproc opencv_imgcodecs opencv_calib3d opencv_features2d opencv_videoio)

install(TARGETS main DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})