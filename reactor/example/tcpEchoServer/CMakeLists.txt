include_directories("${PROJECT_SOURCE_DIR}/include") # add including search path
set(client "TCPEchoClient.exe")
set(server "TCPEchoServer.exe")

set(client_src client.cc echoMsg.pb.cc)
set(server_src server.cc echoMsg.pb.cc)

message("Build ${client} with: ${client_src}")
message("Build ${server} with: ${server_src}")

add_executable(${client} ${client_src})
add_executable(${server} ${server_src})

target_link_libraries(${client} reactor ${PROTOBUF_LIBRARIES})
target_link_libraries(${server} reactor ${PROTOBUF_LIBRARIES})
