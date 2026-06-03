cmake_minimum_required(VERSION 3.15)

project(game_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include common CMake logic (gRPC, Protobuf via vcpkg)
include("../cmake/common.cmake")

# Generate Proto files
kihan_generate_proto(GAME_PROTO_SRCS GAME_PROTO_HDRS 
    "proto/client_game.proto"
    "proto/server_game.proto"
    "proto/router.proto"
)

# Source files
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

# Create executable
add_executable(game_server
    ${SRC_FILES}
    ${GAME_PROTO_SRCS}
    ${GAME_PROTO_HDRS}
)

# Link libraries
target_link_libraries(game_server
    gRPC::grpc++
    gRPC::grpc
    protobuf::libprotobuf
    Threads::Threads
)

# Include directories
target_include_directories(game_server PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)