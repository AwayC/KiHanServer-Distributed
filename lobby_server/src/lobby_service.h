#pragma once

// 必须在生成的 pb 头文件之前包含 grpcpp
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <grpcpp/impl/codegen/service_type.h> // 显式包含以解决“类型不完整”问题
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>

#include "router.grpc.pb.h"
#include "client_lobby.pb.h"
#include "server_game.grpc.pb.h"

struct MatchPlayer {
    uint32_t uid;
    uint32_t conn_id;
    int32_t character_id;
    std::string nickname;
};

class LobbyServiceImpl final : public kihan::internal::LobbyService::Service {
public:
    LobbyServiceImpl();
    ~LobbyServiceImpl();

    grpc::Status HandleRequest(grpc::ServerContext* context, 
                               const kihan::internal::GatewayRequest* request, 
                               kihan::internal::GatewayResponse* response) override;

    grpc::Status ClientDisconnect(grpc::ServerContext* context, 
                                  const kihan::internal::GatewayRequest* request, 
                                  kihan::internal::GatewayResponse* response) override;

    grpc::Status Subscribe(grpc::ServerContext* context, 
                           const kihan::internal::Empty* request, 
                           grpc::ServerWriter<kihan::internal::GatewayResponse>* writer) override;

private:
    void HandleLogin(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleCreateRole(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleMatchGame(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleMatchStop(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleLogout(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleGetPlayerData(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleGetOnlineCount(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);

    void PushToGateway(uint32_t conn_id, uint32_t cmd_id, const std::string& payload);

private:
    std::unordered_set<uint32_t> online_players_;
    std::mutex online_mutex_;

    // Matchmaking queue
    std::deque<MatchPlayer> match_queue_;
    std::mutex match_mutex_;

    // Push stream
    grpc::ServerWriter<kihan::internal::GatewayResponse>* push_writer_ = nullptr;
    std::mutex push_mutex_;

    // GameServer client
    std::unique_ptr<kihan::internal::GameControlService::Stub> game_stub_;

    std::atomic<bool> running_{false};
};

