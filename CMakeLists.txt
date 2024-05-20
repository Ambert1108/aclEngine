cmake_minimum_required(VERSION 3.11)

# project information
project(simpleAscend)

# Compile options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")

if (MSVC)
    add_compile_options("/utf-8")
	add_compile_options("/EHsc")  # deal with MSVC Warning C4530
	add_compile_options("/Od")
    add_compile_options(/w44251 /w44275)
endif (MSVC)
find_package(Threads REQUIRED)

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
#list(APPEND COMMON_DEPEND_LIB acllite avcodec avformat avdevice avutil swresample avfilter swscale)
list(APPEND COMMON_DEPEND_LIB avcodec avformat avdevice avutil swresample avfilter swscale)
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
    ${PROJECT_SOURCE_DIR}/include/
    $ENV{THIRDPART_PATH}/include/acllite
)

# add host lib path
link_directories(
    $ENV{INSTALL_DIR}/runtime/lib64/
    $ENV{THIRDPART_PATH}/lib/
    $ENV{INSTALL_DIR}/driver
)

set(ACLE_DIR "${PROJECT_SOURCE_DIR}/include/aclengine/refer")
#aux_source_directory(ACLE_DIR ACLE_CPP)
set(ACLE_CPP 
    ${ACLE_DIR}/AclLiteApp.cpp 
    ${ACLE_DIR}/AclLiteResource.cpp 
    ${ACLE_DIR}/AclLiteVideoProc.cpp
    #${ACLE_DIR}/CameraCapture.cpp 
    ${ACLE_DIR}/VideoCapture.cpp 
    ${ACLE_DIR}/VdecHelper.cpp 
    ${ACLE_DIR}/AclLiteThread.cpp 
    ${ACLE_DIR}/AclLiteThreadMgr.cpp 
    ${ACLE_DIR}/VideoWriter.cpp
    ${ACLE_DIR}/VencHelper.cpp 
    ${ACLE_DIR}/AclLiteImageProc.cpp 
    ${ACLE_DIR}/ResizeHelper.cpp 
    ${ACLE_DIR}/JpegDHelper.cpp 
    ${ACLE_DIR}/JpegEHelper.cpp 
    ${ACLE_DIR}/PngDHelper.cpp 
    ${ACLE_DIR}/CropAndPasteHelper.cpp 
    ${ACLE_DIR}/AclLiteUtils.cpp 
    #${ACLE_DIR}/AclLiteModel.cpp 
    )

add_subdirectory(samples/decandenc)
#add_subdirectory(samples/encode_test)
add_subdirectory(samples/venc/src)