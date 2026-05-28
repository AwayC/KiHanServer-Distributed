cmake_minimum_required(VERSION 3.15)
project(lobby_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Find gRPC
find_package(gRPC REQUIRED)

# Find MySQL Connector/C++ or libmysqlclient
# On macOS via brew, it might be in /opt/homebrew/opt/mysql-client
find_path(MYSQL_INCLUDE_DIR mysql.h
    HINTS /opt/homebrew/opt/mysql-client/include
          /usr/local/opt/mysql-client/include
          /usr/include/mysql
)
find_library(MYSQL_LIBRARY NAMES mysqlclient
    HINTS /opt/homebrew/opt/mysql-client/lib
          /usr/local/opt/mysql-client/lib
          /usr/lib/mysql
)

if (MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
    message(STATUS "Found MySQL: ${MYSQL_INCLUDE_DIR}")
else()
    message(WARNING "MySQL client not found. Please install mysql-client.")
endif()

# Protobuf/gRPC generation variables
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(HW_PROTO ${PROTO_SRC_DIR}/client_lobby.proto)
set(ROUTER_PROTO ${PROTO_SRC_DIR}/router.proto)

# Helper function to compile proto files
function(generate_grpc_proto SRCS HDRS PROTO_FILE)
    get_filename_component(FIL_WE ${PROTO_FILE} NAME_WE)
    set(PROTO_SRC "${PROTO_GEN_DIR}/${FIL_WE}.pb.cc")
    set(PROTO_HDR "${PROTO_GEN_DIR}/${FIL_WE}.pb.h")
    set(GRPC_SRC "${PROTO_GEN_DIR}/${FIL_WE}.grpc.pb.cc")
    set(GRPC_HDR "${PROTO_GEN_DIR}/${FIL_WE}.grpc.pb.h")

    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR}
        COMMAND protobuf::protoc
        ARGS --grpc_out=${PROTO_GEN_DIR}
             --cpp_out=${PROTO_GEN_DIR}
             -I${PROTO_SRC_DIR}
             --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
             ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
    )

    set(${SRCS} ${PROTO_SRC} ${GRPC_SRC} PARENT_SCOPE)
    set(${HDRS} ${PROTO_HDR} ${GRPC_HDR} PARENT_SCOPE)
endfunction()

generate_grpc_proto(PROTO_CLIENT_SRCS PROTO_CLIENT_HDRS ${HW_PROTO})
generate_grpc_proto(PROTO_ROUTER_SRCS PROTO_ROUTER_HDRS ${ROUTER_PROTO})

# Include directories
include_directories(
    ${PROTO_GEN_DIR}
    ${MYSQL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add source files
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_executable(lobby_server
    ${SRC_FILES}
    ${PROTO_CLIENT_SRCS}
    ${PROTO_ROUTER_SRCS}
)

target_link_libraries(lobby_server
    gRPC::grpc++
    gRPC::grpc
    protobuf::libprotobuf
    ${MYSQL_LIBRARY}
)
