#pragma once
#include <grpcpp/grpcpp.h>
#include "router.grpc.pb.h"
#include "server_game.grpc.pb.h"
#include "room_manager.h"
#include <mutex>

// Wrapper for the gRPC ServerReaderWriter
class GrpcSessionStream : public SessionStream {
public:
    GrpcSessionStream(grpc::ServerReaderWriter<kihan::internal::GatewayResponse, 
                    kihan::internal::GatewayRequest>* stream, 
                    uint32_t conn_id)
        : stream_(stream), conn_id_(conn_id) {}

    void Send(int32_t cmd_id, const std::string& payload) override {
        std::cout << "[GameStream] Sending CmdID=" << cmd_id << " to ConnID=" << conn_id_ << ", PayloadLen=" << payload.length() << std::endl;
        kihan::internal::GatewayResponse resp;
        resp.set_conn_id(conn_id_);
        resp.set_cmd_id(cmd_id);
        resp.set_payload(payload);
        
        std::lock_guard<std::mutex> lock(write_mutex_);
        stream_->Write(resp);
    }

private:
    grpc::ServerReaderWriter<kihan::internal::GatewayResponse, kihan::internal::GatewayRequest>* stream_;
    uint32_t conn_id_;
    std::mutex write_mutex_;
};

// Implements Lobby <-> Game (Unary RPC)
class GameControlServiceImpl final : public kihan::internal::GameControlService::Service {
public:
    grpc::Status CreateRoom(grpc::ServerContext* context, 
                            const kihan::internal::CreateRoomReq* request, 
                            kihan::internal::CreateRoomRsp* response) override;
};

// Implements Gateway <-> Game (Bidirectional Stream)
class GameServiceImpl final : public kihan::internal::GameService::Service {
public:
    grpc::Status StreamBattle(grpc::ServerContext* context, 
                              grpc::ServerReaderWriter<kihan::internal::GatewayResponse, kihan::internal::GatewayRequest>* stream) override;
};