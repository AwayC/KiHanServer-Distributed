cmake_minimum_required(VERSION 3.15)

# 如果没设置工具链但定义了 VCPKG_ROOT，自动关联 vcpkg
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(lobby_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 引入全局公共 CMake 逻辑 (gRPC, Protobuf via vcpkg)
include("../cmake/common.cmake")

# --- 智能跨平台寻找 MySQL Connector/C++ (手动路径优先) ---
if(APPLE)
    # Mac Homebrew 默认路径
    set(MYSQL_CONNECTOR_DIR "/opt/homebrew/opt/mysql-connector-c++")
else()
    # Ubuntu/Linux 手动下载部署的路径
    set(MYSQL_CONNECTOR_DIR "/opt/mysql-connector-cpp")
endif()

find_path(MYSQL_CONNECTOR_INCLUDE_DIR mysqlx/xdevapi.h
    HINTS ${MYSQL_CONNECTOR_DIR}/include
)

# Linux 下库名通常包含版本号，这里加入更多候选名字
find_library(MYSQL_CONNECTOR_LIBRARY 
    NAMES mysqlcppconn8 mysqlcppconnx mysqlcppconn8-static
    HINTS ${MYSQL_CONNECTOR_DIR}/lib64 ${MYSQL_CONNECTOR_DIR}/lib
)

if (MYSQL_CONNECTOR_INCLUDE_DIR AND MYSQL_CONNECTOR_LIBRARY)
    message(STATUS "Found MySQL Connector/C++: ${MYSQL_CONNECTOR_INCLUDE_DIR}")
    message(STATUS "Library Path: ${MYSQL_CONNECTOR_LIBRARY}")
else()
    message(FATAL_ERROR "MySQL Connector/C++ not found! \nMac: brew install mysql-connector-c++\nUbuntu: Download to ${MYSQL_CONNECTOR_DIR}")
endif()

# 编译业务 Proto 文件
kihan_generate_proto(LOBBY_PROTO_SRCS LOBBY_PROTO_HDRS 
    "proto/client_lobby.proto" 
    "proto/router.proto"
)

# 源代码文件
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

# 创建可执行程序
add_executable(lobby_server
    ${SRC_FILES}
    ${LOBBY_PROTO_SRCS}
    ${LOBBY_PROTO_HDRS}
)

# 链接库
target_link_libraries(lobby_server
    gRPC::grpc++
    gRPC::grpc
    protobuf::libprotobuf
    ${MYSQL_CONNECTOR_LIBRARY}
    Threads::Threads
)

# 包含目录
target_include_directories(lobby_server PRIVATE
    ${MYSQL_CONNECTOR_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
