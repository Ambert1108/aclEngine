cmake_minimum_required(VERSION 3.11)

# project information
project(simpleAscend)

# Compile options
add_compile_options(-std=c++11)

add_definitions(-DENABLE_DVPP_INTERFACE)

# Specify target generation path
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  "../out")
set(CMAKE_CXX_FLAGS_DEBUG "-fPIC -O0 -g -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-fPIC -O2 -Wall")

if (NOT DEFINED ENV{INSTALL_DIR})
    MESSAGE(FATAL_ERROR "Not Defined INSTALL_DIR")
endif()
message("install dir:$ENV{INSTALL_DIR}")

if (NOT DEFINED ENV{THIRDPART_PATH})
    MESSAGE(FATAL_ERROR "Not Defined THIRDPART_PATH")
endif()
message("thirdpart path:$ENV{THIRDPART_PATH}")

if (NOT DEFINED ENV{CPU_ARCH})
    MESSAGE(FATAL_ERROR "Not Defined CPU_ARCH")
endif()
message("cpu arch:$ENV{CPU_ARCH}")

add_definitions(-DENABLE_DVPP_INTERFACE)
list(APPEND COMMON_DEPEND_LIB acllite avcodec avformat avdevice avutil swresample avfilter swscale)
if ($ENV{CPU_ARCH} MATCHES "aarch64")
    if(EXISTS "$ENV{INSTALL_DIR}/driver/libmedia_mini.so")
        list(APPEND COMMON_DEPEND_LIB media_mini slog ascend_hal c_sec mmpa)
        add_definitions(-DENABLE_BOARD_CAMARE)
        message(STATUS "arch: arm")
    endif()
endif()

set(CMAKE_PREFIX_PATH "/usr/local/lib64" ${CMAKE_PREFIX_PATH})
find_package(OpenCV REQUIRED)
message(STATUS "version: ${OpenCV_VERSION} ${BUILD_TYPE_NAME}")
message(STATUS "opencv include:${OpenCV_INCLUDE_DIRS}")
message(STATUS "opencv lib:${OpenCV_LIBS}")

# Header path
include_directories(
    ../inc/
    $ENV{INSTALL_DIR}/runtime/include/
	$ENV{THIRDPART_PATH}/include/
    $ENV{THIRDPART_PATH}/include/acllite
)

# add host lib path
link_directories(
    $ENV{INSTALL_DIR}/runtime/lib64/
    $ENV{THIRDPART_PATH}/lib/
    $ENV{INSTALL_DIR}/driver
)

add_subdirectory(samples/decandenc)