set(SAMPLE_NAME decandenc)

add_executable(${SAMPLE_NAME} test.cpp)
#add_executable(${SAMPLE_NAME} testEncoder.cpp ${ACLE_CPP})

# Get superior directory
#get_filename_component(INC_DIR ${PROJECT_SOURCE_DIR}/.. ABSOLUTE)
#message("add ${INC_DIR}/include")
message("include dir: ${PROJECT_SOURCE_DIR}/include")
target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(${SAMPLE_NAME} stdc++ ${PROJECT_LIBS} pthread)

#install(TARGETS decandenc DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})