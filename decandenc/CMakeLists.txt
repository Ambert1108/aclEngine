# CMake lowest version requirement
cmake_minimum_required(VERSION 3.5.1)

# project information
project(decandenc)

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

if (NOT DEFINED ENV{THIRDPART_PATH})
    MESSAGE(FATAL_ERROR "Not Defined THIRDPART_PATH")
endif()

if (NOT DEFINED ENV{CPU_ARCH})
    MESSAGE(FATAL_ERROR "Not Defined CPU_ARCH")
endif()

add_definitions(-DENABLE_DVPP_INTERFACE)
list(APPEND COMMON_DEPEND_LIB acllite avcodec avformat avdevice avutil swresample avfilter swscale)
if ($ENV{CPU_ARCH} MATCHES "aarch64")
    if(EXISTS "$ENV{INSTALL_DIR}/driver/libmedia_mini.so")
        list(APPEND COMMON_DEPEND_LIB media_mini slog ascend_hal c_sec mmpa)
        add_definitions(-DENABLE_BOARD_CAMARE)
        message(STATUS "arch: arm")
    endif()
endif()

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
    $ENV{INSTALL_DIR}/runtime/lib64/stub
    $ENV{THIRDPART_PATH}/lib/
    $ENV{INSTALL_DIR}/driver
)

add_executable(${PROJECT_NAME}
        test.cpp)

# Get superior directory
get_filename_component(INC_DIR ${PROJECT_SOURCE_DIR}/.. ABSOLUTE)
message("add ${INC_DIR}/include")
#target_include_directories(${PROJECT_NAME} PUBLIC ${INC_DIR}/include)

if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    message("1")
    target_link_libraries(${PROJECT_NAME}
        libascendcl libacl_cblas libacl_dvpp opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio pthread)
else()
    message("2")
    target_link_libraries(${PROJECT_NAME}
        ascendcl acl_cblas acl_dvpp stdc++ ${COMMON_DEPEND_LIB} opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio pthread)
endif()

install(TARGETS main DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})