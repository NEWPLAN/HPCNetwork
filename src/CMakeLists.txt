CMAKE_MINIMUM_REQUIRED(VERSION 3.0) #cmake版本要求
project(Reactor)

message("Building Reactor now ...") #打印消息

set(CMAKE_BUILD_TYPE "Debug")

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
message("${Protobuf_INCLUDE_DIRS}")
message("${CMAKE_CURRENT_BINARY_DIR}")
#protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS foo.proto)
#protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS EXPORT_MACRO DLL_EXPORT foo.proto)
#protobuf_generate_python(PROTO_PY foo.proto)
#add_executable(bar bar.cc ${PROTO_SRCS} ${PROTO_HDRS})
#target_link_libraries(bar ${Protobuf_LIBRARIES})

set(RUNNING_LIB_PATH "./src")
set(CMAKE_CXX_STANDARD 11) # 设置std=c++11

# 设置debug或者release的编译参数
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb -fPIC -Wno-deprecated")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -fPIC -Wno-deprecated")


#添加一个子模块，编译到lib目录下去
add_subdirectory(${RUNNING_LIB_PATH} lib) # add a submodule, and compile it to the "build/lib"
#添加一个子模块，编译到lib目录下去
#add_subdirectory("benchmark") # add a submodule, and compile it to the "build/lib"
#添加一个子模块，编译到lib目录下去
#add_subdirectory("example") # add a submodule, and compile it to the "build/lib"
#add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(example)

