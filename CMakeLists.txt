cmake_minimum_required(VERSION 3.11)

# project information
project(simpleAscend)
set(MODULE MINDX)
set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/include)
set(PROJECT_LIBS)
message("CMAKE_PREFIX_PATH:" ${CMAKE_PREFIX_PATH})

if(${MODULE} MATCHES "NORMAL")
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
    set(CMAKE_SKIP_RPATH TRUE)

    set(CMAKE_PREFIX_PATH "/usr/local/lib64" ${CMAKE_PREFIX_PATH})
    set(CMAKE_INCLUDE_PATH "/usr/local/ffmpeg/include")
    set(CMAKE_LIBRARY_PATH "/usr/local/ffmpeg/lib")
		
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
    if ($ENV{CPU_ARCH} MATCHES "aarch64")
	    if(EXISTS "$ENV{INSTALL_DIR}/driver/libmedia_mini.so")
		    list(APPEND COMMON_DEPEND_LIB media_mini slog ascend_hal c_sec mmpa)
		    add_definitions(-DENABLE_BOARD_CAMARE)
		    message(STATUS "arch: arm")
	    endif()
    endif()
    set(CMAKE_PREFIX_PATH "$ENV{INSTALL_DIR}/runtime/include/" ${CMAKE_PREFIX_PATH})
    set(CMAKE_PREFIX_PATH "$ENV{INSTALL_DIR}/runtime/lib64/stub/" ${CMAKE_PREFIX_PATH})
    set(CMAKE_PREFIX_PATH "$ENV{THIRDPART_PATH}/lib/" ${CMAKE_PREFIX_PATH})

    #find FFmpeg include and library file
    message("====== FIND FFMPEG LIBRARY AND INCLUDE ======")
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
    list(APPEND PROJECT_INCLUDE ${FFMPEG_INCLUDE_DIR})
    find_library(AVCODEC_LIBRARY avcodec REQUIRED)
    list(APPEND PROJECT_LIBS ${AVCODEC_LIBRARY})
    find_library(AVFORMAT_LIBRARY avformat)
    list(APPEND PROJECT_LIBS ${AVFORMAT_LIBRARY})
    find_library(AVUTIL_LIBRARY avutil)
    list(APPEND PROJECT_LIBS ${AVUTIL_LIBRARY})
    find_library(AVDEVICE_LIBRARY avdevice)
    list(APPEND PROJECT_LIBS ${AVDEVICE_LIBRARY})
    find_library(AVFILTER_LIBRARY avfilter)
    list(APPEND PROJECT_LIBS ${AVFILTER_LIBRARY})
    find_library(SWRESAMPLE_LIBRARY swresample)
    list(APPEND PROJECT_LIBS ${SWRESAMPLE_LIBRARY})
    find_library(SWSCALE_LIBRARY swscale)
    list(APPEND PROJECT_LIBS ${SWSCALE_LIBRARY})
    message(STATUS "ffmpeg include:${FFMPEG_INCLUDE_DIR}")
    message(STATUS "avcodec lib:${AVCODEC_LIBRARY}")
    message(STATUS "avformat lib:${AVFORMAT_LIBRARY}")
    message(STATUS "avutil lib:${AVUTIL_LIBRARY}")
    message(STATUS "avdevice lib:${AVDEVICE_LIBRARY}")
    message(STATUS "avfilter lib:${AVFILTER_LIBRARY}")
    message(STATUS "swresample lib:${SWRESAMPLE_LIBRARY}")
    message(STATUS "swscale lib:${SWSCALE_LIBRARY}")

    #find Ascend include and library file
    find_path(ACL_INCLUDE_DIR "acl/acl.h" REQUIRED)
    list(APPEND PROJECT_INCLUDE ${ACL_INCLUDE_DIR})
    find_library(CL_LIB ascendcl REQUIRED)
    list(APPEND PROJECT_LIBS ${CL_LIB})
    find_library(CBLAS_LIB acl_cblas REQUIRED)
    list(APPEND PROJECT_LIBS ${CBLAS_LIB})
    find_library(DVPP_LIB acl_dvpp REQUIRED)
    list(APPEND PROJECT_LIBS ${DVPP_LIB})
    message("====== FIND ACL LIBRARY AND INCLUDE ======")
    message("ACL_INCLUDE_DIR->${ACL_INCLUDE_DIR}")
    message("CL_LIB->${CL_LIB}")
    message("CBLAS_LIB->${CBLAS_LIB}")
    message("DVPP_LIB->${DVPP_LIB}")

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
    add_subdirectory(samples/cpp)
    #add_subdirectory(samples/encode_test)
    #add_subdirectory(samples/venc/src)
    #add_subdirectory(samples/png)
    #add_subdirectory(samples/jpg)
    #add_subdirectory(samples/cropandpaste)

elseif(${MODULE} MATCHES "MINDX") 
    #set(CMAKE_CXX_STANDARD 17)
    #set(CMAKE_CXX_STANDARD_REQUIRED ON)
    #set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")
    #
    #add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)  
    #add_definitions(-Dgoogle=mindxsdk_private)      
    #add_compile_options(-std=c++17 -fPIC -fstack-protector-all -Wall)
    #
    #set(CMAKE_CXX_FLAGS_DEBUG "-g")                           
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro,-z,now,-z,noexecstack -s -pie")
    #set(CMAKE_SKIP_RPATH TRUE)
    #
    #find_package(Threads REQUIRED)
    #
    ## Specify target generation path
    ##set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  "../out")
    #set(CMAKE_CXX_FLAGS_DEBUG "-fPIC -O0 -g -Wall")
    #set(CMAKE_CXX_FLAGS_RELEASE "-fPIC -O2 -Wall")
    ##set(CMAKE_SKIP_RPATH TRUE)
    #
    #set(CMAKE_PREFIX_PATH "/usr/local/lib64" ${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "/usr/local/ffmpeg/" ${CMAKE_PREFIX_PATH})
    ##set(CMAKE_PREFIX_PATH "/usr/local/opencv/" ${CMAKE_PREFIX_PATH})
    #set(MX_SDK_HOME $ENV{MX_SDK_HOME})      
    #set(ASCEND_HOME_PATH $ENV{ASCEND_HOME_PATH})
    #message("Ascend home: ${ASCEND_HOME_PATH}/${ARCH_PATTERN}")
    #
    #if (NOT DEFINED ENV{MX_SDK_HOME})      
    #  string(REGEX REPLACE "(.*)/(.*)/(.*)/(.*)" "\\1" MX_SDK_HOME  #${CMAKE_CURRENT_SOURCE_DIR})
    #  message(STATUS "set default MX_SDK_HOME: ${MX_SDK_HOME}")
    #else ()
    #  message(STATUS "env MX_SDK_HOME: ${MX_SDK_HOME}")
    #endif()
    #
    #if (NOT DEFINED ENV{INSTALL_DIR})
	#    MESSAGE(FATAL_ERROR "Not Defined INSTALL_DIR")
    #endif()
    #message("install dir:$ENV{INSTALL_DIR}")
	#	
    #if (NOT DEFINED ENV{THIRDPART_PATH})
	#    MESSAGE(FATAL_ERROR "Not Defined THIRDPART_PATH")
    #endif()
    #message("thirdpart path:$ENV{THIRDPART_PATH}")
	#	
    #if (NOT DEFINED ENV{CPU_ARCH})
	#    MESSAGE(FATAL_ERROR "Not Defined CPU_ARCH")
    #endif()
    #message("cpu arch:$ENV{CPU_ARCH}")
    #
    #add_definitions(-DENABLE_DVPP_INTERFACE)
    #if ($ENV{CPU_ARCH} MATCHES "aarch64")
	#    if(EXISTS "$ENV{INSTALL_DIR}/driver/libmedia_mini.so")
	#	    list(APPEND COMMON_DEPEND_LIB media_mini slog ascend_hal c_sec mmpa)
	#	    add_definitions(-DENABLE_BOARD_CAMARE)
	#	    message(STATUS "arch: arm")
	#    endif()
    #endif()
    #set(CMAKE_PREFIX_PATH "${MX_SDK_HOME}/include/" ${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${MX_SDK_HOME}/opensource/include/" #${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "$ENV{INSTALL_DIR}/runtime/include/" #${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${ASCEND_HOME_PATH}/${ARCH_PATTERN}/include" #${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${ASCEND_HOME_PATH}/${ARCH_PATTERN}/runtime/include" #${CMAKE_PREFIX_PATH})
    #
    #set(CMAKE_PREFIX_PATH "${MX_SDK_HOME}/lib/" ${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${MX_SDK_HOME}/lib/modelpostprocessors/" #${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${MX_SDK_HOME}/opensource/lib/" ${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${MX_SDK_HOME}/opensource/lib64/" #${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "$ENV{INSTALL_DIR}/runtime/lib64/stub/" #${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "$ENV{THIRDPART_PATH}/lib/" ${CMAKE_PREFIX_PATH})
    #set(CMAKE_PREFIX_PATH "${ASCEND_HOME_PATH}/atc/lib64" ${CMAKE_PREFIX_PATH})
    #
    ##find Ascend include and library file
    #message("====== FIND ACL LIBRARY AND INCLUDE ======")
    #find_path(ACL_INCLUDE_DIR "acl/acl.h" REQUIRED)
    #list(APPEND PROJECT_INCLUDE ${ACL_INCLUDE_DIR})
    #find_library(CL_LIB ascendcl REQUIRED)
    #list(APPEND PROJECT_LIBS ${CBLAS_LIB})
    #find_library(DVPP_LIB acl_dvpp REQUIRED)
    #list(APPEND PROJECT_LIBS ${CL_LIB})
    #find_library(CBLAS_LIB acl_cblas REQUIRED)
    #list(APPEND PROJECT_LIBS ${DVPP_LIB})
    #message("ACL_INCLUDE_DIR->${ACL_INCLUDE_DIR}")
    #message("CL_LIB->${CL_LIB}")
    #message("DVPP_LIB->${DVPP_LIB}")
    #message("CBLAS_LIB->${CBLAS_LIB}")
    #
    ##find mxVision include and library file
    #message("====== FIND MINDX LIBRARY AND INCLUDE ======")
    #find_path(MX_INCLUDE_DIR "MxBase/MxBase.h" REQUIRED)
    #list(APPEND PROJECT_INCLUDE ${MX_INCLUDE_DIR})
    #list(APPEND PROJECT_INCLUDE "${MX_SDK_HOME}/include/")
    #list(APPEND PROJECT_INCLUDE "${MX_SDK_HOME}/opensource/include/")
    #find_library(BASE_LIB mxbase REQUIRED)
    #list(APPEND PROJECT_LIBS ${BASE_LIB})
    ##find_library(PLUGIN_LIB plugintoolkit REQUIRED)
    ##list(APPEND PROJECT_LIBS ${PLUGIN_LIB})
    ##find_library(DATA_LIB mxpidatatype REQUIRED)
    ##list(APPEND PROJECT_LIBS ${DATA_LIB})
    ##find_library(STREAM_LIB streammanager REQUIRED)
    ##list(APPEND PROJECT_LIBS ${STREAM_LIB})
    #message("MX_INCLUDE_DIR->${MX_INCLUDE_DIR}")
    #message("BASE_LIB->${BASE_LIB}")
    ##message("PLUGIN_LIB->${PLUGIN_LIB}")
    ##message("DATA_LIB->${DATA_LIB}")
    ##message("STREAM_LIB->${STREAM_LIB}")
    #
    #message("====== FIND OPENCV LIBRARY AND INCLUDE ======")
    #find_package(OpenCV REQUIRED)
    ##list(APPEND PROJECT_INCLUDE ${OpenCV_INCLUDE_DIRS})
    ##list(APPEND PROJECT_LIBS ${OpenCV_LIBS})
    #list(APPEND PROJECT_INCLUDE ${MX_SDK_HOME}/opensource/include/opencv4/)
    #find_library(openCV_LIB opencv_world  REQUIRED)
    #list(APPEND PROJECT_LIBS ${openCV_LIB})
    ##message(STATUS "version: ${OpenCV_VERSION} ${BUILD_TYPE_NAME}")
    #message(STATUS "opencv include:${MX_SDK_HOME}/opensource/include/opencv4/")
    #message(STATUS "opencv lib:${openCV_LIB}")
    
    #message(" ====== FIND FREETYPE LIBRARY AND INCLUDE ======")
    #set(FREETYPE_DIR "/usr/include/freetype2")
    #find_library(FREETYPE_LIB freetype  REQUIRED)
    #list(APPEND PROJECT_INCLUDE ${FREETYPE_DIR})
    #list(APPEND PROJECT_LIBS ${FREETYPE_LIB})
    #message("FREETYPE_DIR->${FREETYPE_DIR}")
    #message("FREETYPE_LIB->${FREETYPE_LIB}")

    add_subdirectory(samples/mindX)
endif()