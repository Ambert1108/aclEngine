# project information
set(SAMPLE_NAME mindXTest)
#add_executable(${SAMPLE_NAME} test.cpp)
#add_executable(${SAMPLE_NAME} test2.cpp)
add_executable(${SAMPLE_NAME} blendImagesTest.cpp)
#add_executable(${SAMPLE_NAME} warpAffineTest.cpp ${ACLE})
#add_executable(${SAMPLE_NAME} replaceImagesTest.cpp)
#add_executable(${SAMPLE_NAME} acleTest.cpp ${ACLE})

target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(${SAMPLE_NAME} PUBLIC ${PROJECT_LIBS})
file(COPY "${PROJECT_SOURCE_DIR}/resources/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")