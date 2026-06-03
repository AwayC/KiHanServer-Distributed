#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "game_service.h"
#include "room_manager.h"
#include <thread>

void RunServer() {
    std::string server_address("0.0.0.0:9002");
    GameServiceImpl game_service;
    GameControlServiceImpl control_service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&game_service);
    builder.RegisterService(&control_service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "[Main] GameServer listening on " << server_address << std::endl;

    // Start the game loop in a separate thread
    std::thread game_loop_thread([]() {
        RoomManager::GetInstance().RunLoop();
    });

    server->Wait();
    RoomManager::GetInstance().Stop();
    game_loop_thread.join();
}

int main(int argc, char** argv) {
    std::cout << "[Main] GameServer starting..." << std::endl;
    RunServer();
    return 0;
}