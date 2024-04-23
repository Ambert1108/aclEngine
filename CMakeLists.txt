# CMake lowest version requirement
cmake_minimum_required(VERSION 3.11)

project(simpleAscend)

#add_subdirectory("./venc/src")
#add_subdirectory("./vdec/src")
#add_subdirectory("./decode")
add_subdirectory("./yolov3_dvpp")
add_subdirectory("./decandenc")