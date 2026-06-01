# cmake/common.cmake

# 1. 寻找核心依赖 (gRPC, Protobuf)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Threads REQUIRED)

# 定义全局路径变量 (参考官方样例)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# 2. 定义 Protobuf/gRPC 代码生成函数
# 参数:
#   GEN_SRCS: 存放生成的 .cc 文件的变量名
#   GEN_HDRS: 存放生成的 .h 文件的变量名
#   PROTO_FILES: 需要编译的 .proto 文件列表
function(kihan_generate_proto GEN_SRCS GEN_HDRS)
    if(NOT ARGN)
        message(SEND_ERROR "Error: kihan_generate_proto called without any proto files")
        return()
    endif()

    # 设定生成目录
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    if(NOT EXISTS "${PROTO_GEN_DIR}")
        file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")
    endif()

    set(${GEN_SRCS})
    set(${GEN_HDRS})

    foreach(PROTO_FILE ${ARGN})
        get_filename_component(FIL_WE ${PROTO_FILE} NAME_WE)
        get_filename_component(ABS_FIL ${PROTO_FILE} ABSOLUTE)
        get_filename_component(ABS_PATH ${ABS_FIL} PATH)

        set(P_SRC "${PROTO_GEN_DIR}/${FIL_WE}.pb.cc")
        set(P_HDR "${PROTO_GEN_DIR}/${FIL_WE}.pb.h")
        set(G_SRC "${PROTO_GEN_DIR}/${FIL_WE}.grpc.pb.cc")
        set(G_HDR "${PROTO_GEN_DIR}/${FIL_WE}.grpc.pb.h")

        add_custom_command(
            OUTPUT "${P_SRC}" "${P_HDR}" "${G_SRC}" "${G_HDR}"
            COMMAND ${_PROTOBUF_PROTOC}
            ARGS --grpc_out=${PROTO_GEN_DIR}
                 --cpp_out=${PROTO_GEN_DIR}
                 -I ${ABS_PATH}
                 --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
                 ${ABS_FIL}
            DEPENDS ${ABS_FIL}
            COMMENT "Generating gRPC/Protobuf code for ${FIL_WE}"
            VERBATIM
        )

        list(APPEND ${GEN_SRCS} "${P_SRC}" "${G_SRC}")
        list(APPEND ${GEN_HDRS} "${P_HDR}" "${G_HDR}")
    endforeach()

    # 将变量传回父作用域
    set(${GEN_SRCS} ${${GEN_SRCS}} PARENT_SCOPE)
    set(${GEN_HDRS} ${${GEN_HDRS}} PARENT_SCOPE)

    # 自动把生成目录加入包含路径
    include_directories(${PROTO_GEN_DIR})
endfunction()
