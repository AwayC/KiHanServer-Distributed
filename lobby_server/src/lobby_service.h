#pragma once

// 必须在生成的 pb 头文件之前包含 grpcpp
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <grpcpp/impl/codegen/service_type.h> // 显式包含以解决“类型不完整”问题
#include <unordered_set>
#include <mutex>

#include "router.grpc.pb.h"
#include "client_lobby.pb.h"

class LobbyServiceImpl final : public kihan::internal::LobbyService::Service {
public:
    grpc::Status HandleRequest(grpc::ServerContext* context, 
                               const kihan::internal::GatewayRequest* request, 
                               kihan::internal::GatewayResponse* response) override;

    grpc::Status ClientDisconnect(grpc::ServerContext* context, 
                                  const kihan::internal::GatewayRequest* request, 
                                  kihan::internal::GatewayResponse* response) override;

private:
    void HandleLogin(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleCreateRole(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleMatchGame(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleMatchStop(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleLogout(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleGetPlayerData(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);
    void HandleGetOnlineCount(const kihan::internal::GatewayRequest* req, kihan::internal::GatewayResponse* rsp);

private:
    std::unordered_set<std::string> online_players_;
    std::mutex online_mutex_;
};
