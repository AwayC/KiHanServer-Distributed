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

include(FetchContent)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

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
    nlohmann_json::nlohmann_json
)

# Include directories
target_include_directories(game_server PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)