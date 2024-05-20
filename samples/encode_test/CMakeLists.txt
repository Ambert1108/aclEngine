set(SAMPLE_NAME encTest)

add_executable(${SAMPLE_NAME} main.cpp ${ACLE_CPP})

# Get superior directory
#get_filename_component(INC_DIR ${PROJECT_SOURCE_DIR}/.. ABSOLUTE)
#message("add ${INC_DIR}/include")
message("include dir: ${PROJECT_SOURCE_DIR}/include")
target_include_directories(${SAMPLE_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(${SAMPLE_NAME} ascendcl acl_cblas acl_dvpp stdc++ ${COMMON_DEPEND_LIB} pthread)

#install(TARGETS decandenc DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})