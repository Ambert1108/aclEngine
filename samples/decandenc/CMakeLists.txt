set(SAMPLE_NAME decandenc)

add_executable(${SAMPLE_NAME} test.cpp)

# Get superior directory
#get_filename_component(INC_DIR ${PROJECT_SOURCE_DIR}/.. ABSOLUTE)
#message("add ${INC_DIR}/include")
#message("include dir: ${PROJECT_SOURCE_DIR}/include")
target_include_directories(${SAMPLE_NAME} PUBLIC 
        ${PROJECT_SOURCE_DIR}/include)

if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    message("1")
    target_link_libraries(${SAMPLE_NAME} libascendcl libacl_cblas libacl_dvpp opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio pthread)
else()
    message("2")
    target_link_libraries(${SAMPLE_NAME}
        ascendcl acl_cblas acl_dvpp stdc++ ${COMMON_DEPEND_LIB} pthread)
endif()

#install(TARGETS decandenc DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})