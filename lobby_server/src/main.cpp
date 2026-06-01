#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include "lobby_service.h"
#include "db_manager.h"

void RunServer() {
    std::string server_address("0.0.0.0:9001");
    LobbyServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "LobbyServer listening on " << server_address << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    std::cout << "[Main] LobbyServer starting..." << std::endl;
    // In a real application, you would load these parameters from config.json
    // For this boilerplate, we'll hardcode the localhost DB connection
    std::cout << "[Main] Initializing Database (127.0.0.1:33060)..." << std::endl;
    if (!DBManager::GetInstance().Init("127.0.0.1", 33060, "root", "123456", "kihan_db")) {
        std::cerr << "[Main] Failed to initialize database. Exiting..." << std::endl;
        return 1;
    }
    std::cout << "[Main] Database initialized successfully." << std::endl;

    RunServer();
    return 0;
}
