include_directories("${PROJECT_SOURCE_DIR}/include") # add including search path
set(benchmark "benchmark.exe")
set(server "server.exe")

set(benchmark_src benchmark.cc echoMsg.pb.cc)
set(server_src server.cc echoMsg.pb.cc)

message("Build ${benchmark} with: ${benchmark_src}")
message("Build ${server} with: ${server_src}")

add_executable(${benchmark} ${benchmark_src})
add_executable(${server} ${server_src})

target_link_libraries(${benchmark} reactor ${PROTOBUF_LIBRARIES})
target_link_libraries(${server} reactor ${PROTOBUF_LIBRARIES})
