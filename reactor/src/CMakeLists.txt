include_directories("${PROJECT_SOURCE_DIR}/include") # add including search path
#set(reactor_lib "reactor")
set(reactor "reactor")
aux_source_directory(. DIR_LIB_SRCS)

message("building ${reactor} with ${DIR_LIB_SRCS}")

add_library(${reactor} SHARED ${DIR_LIB_SRCS})
message("Building dynamic lib: ${reactor}.so")

add_library("${reactor}_static" STATIC  ${DIR_LIB_SRCS})
#add_library("${reactor}" STATIC  ${DIR_LIB_SRCS})
message("Building static lib: ${reactor}.a")

# 指定静态库的输出名称
set_target_properties("${reactor}_static" PROPERTIES OUTPUT_NAME "${reactor}")
# 使动态库和静态库同时存在
set_target_properties(${reactor} PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties("${reactor}_static" PROPERTIES CLEAN_DIRECT_OUTPUT 1)