# project information
set(SAMPLE_NAME aclffmpegTest)
#add_executable(${SAMPLE_NAME} decoder.cpp ${ACLE})
#add_executable(${SAMPLE_NAME} encoder.cpp  ${ACLE})
#add_executable(${SAMPLE_NAME} multiEncode.cpp)
add_executable(${SAMPLE_NAME} dec.cpp ${ACLE})

target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(${SAMPLE_NAME} PUBLIC ${PROJECT_LIBS})