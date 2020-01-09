include_directories("${PROJECT_SOURCE_DIR}/include") # add including search path
set(target "timerServer.exe")

aux_source_directory(. DIR_LIB_SRCS)

message("Build ${target} with: ${DIR_LIB_SRCS}")

add_executable(${target} ${DIR_LIB_SRCS})

target_link_libraries(${target} reactor ${PROTOBUF_LIBRARIES})
