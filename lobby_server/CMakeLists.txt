cmake_minimum_required(VERSION 3.15)
project(lobby_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 引入全局公共 CMake 逻辑
include("../cmake/common.cmake")

# Find MySQL Connector/C++ (X DevAPI) - 这个是 Lobby 独有的，不放在 common
set(MYSQL_CONNECTOR_DIR "/opt/homebrew/opt/mysql-connector-c++")
find_path(MYSQL_INCLUDE_DIR mysqlx/xdevapi.h
    HINTS ${MYSQL_CONNECTOR_DIR}/include
)
find_library(MYSQL_LIBRARY NAMES mysqlcppconn8 mysqlcppconnx
    HINTS ${MYSQL_CONNECTOR_DIR}/lib
)

if (MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
    message(STATUS "Found MySQL Connector/C++: ${MYSQL_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "MySQL Connector/C++ not found. Please brew install mysql-connector-c++")
endif()

# 编译业务 Proto 文件
# 调用 common.cmake 中定义的函数
kihan_generate_proto(LOBBY_PROTO_SRCS LOBBY_PROTO_HDRS 
    "proto/client_lobby.proto" 
    "proto/router.proto"
)

# 包含路径
include_directories(
    ${MYSQL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 源代码文件
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

# 创建可执行程序
add_executable(lobby_server
    ${SRC_FILES}
    ${LOBBY_PROTO_SRCS}
    ${LOBBY_PROTO_HDRS}
)

# 链接库 (使用 common.cmake 找好的全局 Target)
target_link_libraries(lobby_server
    gRPC::grpc++
    gRPC::grpc
    protobuf::libprotobuf
    ${MYSQL_LIBRARY}
    Threads::Threads
)
