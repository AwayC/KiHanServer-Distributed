cmake_minimum_required(VERSION 3.15)

project(lobby_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 引入全局公共 CMake 逻辑 (gRPC, Protobuf via vcpkg)
include("../cmake/common.cmake")

# ---  MySQL Connector/C++ (手动路径优先) ---
find_path(MYSQL_CONNECTOR_INCLUDE_DIR mysqlx/xdevapi.h
    HINTS
        /opt/homebrew/opt/mysql-connector-c++/include   # Mac Homebrew 路径
        /usr/include/mysql-cppconn                      # Ubuntu deb 包真实路径
)

find_library(MYSQL_CONNECTOR_LIBRARY 
    NAMES mysqlcppconnx mysqlcppconn8 mysqlcppconnx-static mysqlcppconn8-static
    HINTS
        /opt/homebrew/opt/mysql-connector-c++/lib       # Mac Homebrew 路径
        /usr/lib/x86_64-linux-gnu                       # Ubuntu deb 包真实标准库路径
        /usr/lib
)

# 3. 校验并打印
if (MYSQL_CONNECTOR_INCLUDE_DIR AND MYSQL_CONNECTOR_LIBRARY)
    message(STATUS "成功找到 MySQL Connector/C++ !")
    message(STATUS "头文件路径: ${MYSQL_CONNECTOR_INCLUDE_DIR}")
    message(STATUS "库文件路径: ${MYSQL_CONNECTOR_LIBRARY}")
else()
    message(FATAL_ERROR "未找到 MySQL Connector/C++！\n"
                        "Mac 请运行: brew install mysql-connector-c++\n"
                        "Ubuntu 请运行: sudo apt install libmysqlcppconn-dev")
endif()

# 编译业务 Proto 文件
kihan_generate_proto(LOBBY_PROTO_SRCS LOBBY_PROTO_HDRS 
    "proto/client_lobby.proto" 
    "proto/router.proto"
    "../game_server/proto/server_game.proto"
)

# 源代码文件
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

include(FetchContent)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

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
    nlohmann_json::nlohmann_json
)

# 包含目录
target_include_directories(lobby_server PRIVATE
    ${MYSQL_CONNECTOR_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
