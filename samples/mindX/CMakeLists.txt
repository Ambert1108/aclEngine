set(SAMPLE_NAME mindxTest)
add_executable(${SAMPLE_NAME} test.cpp)
target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(${SAMPLE_NAME} ${PROJECT_LIBS} glog mxbase streammanager mindxsdk_protobuf)