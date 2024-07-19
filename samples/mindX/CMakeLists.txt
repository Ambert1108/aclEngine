# project information
set(SAMPLE_NAME mindXTest)
#
#set(MX_SDK_HOME $ENV{MX_SDK_HOME})      
#
#if (NOT DEFINED ENV{MX_SDK_HOME})      
#  string(REGEX REPLACE "(.*)/(.*)/(.*)/(.*)" "\\1" MX_SDK_HOME  #${CMAKE_CURRENT_SOURCE_DIR})
#  message(STATUS "set default MX_SDK_HOME: ${MX_SDK_HOME}")
#else ()
#  message(STATUS "env MX_SDK_HOME: ${MX_SDK_HOME}")
#endif()
#
## Compile options
#add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)  
#add_definitions(-Dgoogle=mindxsdk_private)      
#add_compile_options(-std=c++17 -fPIC -fstack-protector-all -Wall)   
#
#set(CMAKE_CXX_FLAGS_DEBUG "-g")                           
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro,-z,now,-#z,noexecstack -s -pie")
#set(CMAKE_SKIP_RPATH TRUE)
#
#set(ASCEND_HOME_PATH $ENV{ASCEND_HOME_PATH})
#message("Ascend home: ${ASCEND_HOME_PATH}->${ARCH_PATTERN}")
#
## Header path  
#include_directories(
#  ${PROJECT_SOURCE_DIR}/include
#  ${OpenCV_INCLUDE_DIRS}
#  ${FREETYPE_DIR}
#  ${MX_SDK_HOME}/include/     
#  ${MX_SDK_HOME}/opensource/include/
#  ${MX_SDK_HOME}/opensource/include/opencv4/
#  $ENV{INSTALL_DIR}/runtime/include/
#  ${ASCEND_HOME_PATH}/${ARCH_PATTERN}/include
#  ${ASCEND_HOME_PATH}/${ARCH_PATTERN}/runtime/include
#)
#
# #add host lib path   
#link_directories(
#  ${MX_SDK_HOME}/lib/
#  ${MX_SDK_HOME}/lib/modelpostprocessors/
#  ${MX_SDK_HOME}/opensource/lib/
#  ${MX_SDK_HOME}/opensource/lib64/
#  ${ASCEND_HOME_PATH}/atc/lib64
#  $ENV{INSTALL_DIR}/runtime/lib64/stub/
#  $ENV{THIRDPART_PATH}/lib/
#  /usr/lib64/
#)

#add_executable(${SAMPLE_NAME} test.cpp)
#add_executable(${SAMPLE_NAME} test2.cpp)
add_executable(${SAMPLE_NAME} test3.cpp)
target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(${SAMPLE_NAME} PUBLIC ${PROJECT_LIBS})
#target_link_libraries(${SAMPLE_NAME} PUBLIC 
#	ascendcl
#	acl_dvpp
#	acl_cblas
#	mxbase
#	#streammanager 
#	#glog
#	opencv_world
#	#freetype
#)
#message("PROJECT_LIBS->${PROJECT_LIBS}")

#install(TARGETS main DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY "${PROJECT_SOURCE_DIR}/resources/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")