#pragma once

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
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
};
