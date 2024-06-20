set(SAMPLE_NAME cppTest)

add_executable(${SAMPLE_NAME} test.cpp)
target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(${SAMPLE_NAME} stdc++ ${PROJECT_LIBS} pthread)

#install(TARGETS decandenc DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})