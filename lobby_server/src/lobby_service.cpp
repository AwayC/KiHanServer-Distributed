#include "lobby_service.h"
#include "db_manager.h"
#include <iostream>

using grpc::Status;
using grpc::ServerContext;
using namespace kihan::internal;
using namespace kihan::api;

Status LobbyServiceImpl::HandleRequest(ServerContext* context, const GatewayRequest* request, GatewayResponse* response) {
    response->set_conn_id(request->conn_id());
    
    uint32_t cmd_id = request->cmd_id();
    std::cout << "[Lobby] Received cmd_id: " << cmd_id << " from uid: " << request->uid() << std::endl;

    switch (cmd_id) {
        case 1001: HandleLogin(request, response); break;
        case 1002: HandleLogout(request, response); break;
        case 1003: HandleCreateRole(request, response); break;
        case 1005: HandleMatchGame(request, response); break;
        case 1006: HandleMatchStop(request, response); break;
        case 1008: HandleGetPlayerData(request, response); break;
        default:
            std::cerr << "[Lobby] Unknown cmd_id: " << cmd_id << std::endl;
            break;
    }

    return Status::OK;
}

Status LobbyServiceImpl::ClientDisconnect(ServerContext* context, const GatewayRequest* request, GatewayResponse* response) {
    std::cout << "[Lobby] Client disconnected - uid: " << request->uid() << " conn_id: " << request->conn_id() << std::endl;
    return Status::OK;
}

void LobbyServiceImpl::HandleLogin(const GatewayRequest* req, GatewayResponse* rsp) {
    LoginReq login_req;
    if (!login_req.ParseFromString(req->payload())) {
        std::cerr << "Failed to parse LoginReq" << std::endl;
        return;
    }

    auto data = DBManager::GetInstance().GetPlayerData(req->uid());
    
    if (data) {
        LoginRsp login_rsp;
        login_rsp.set_err_code(0);
        
        auto* p_info = login_rsp.mutable_player();
        p_info->set_uid(data->uid);
        p_info->set_nickname(data->nickname);
        p_info->set_data_json(data->data_json);
        
        rsp->set_cmd_id(1001);
        login_rsp.SerializeToString(rsp->mutable_payload());
    } else {
        // Return CreateRoleNtf (CmdID 1004)
        CreateRoleNtf ntf;
        rsp->set_cmd_id(1004);
        ntf.SerializeToString(rsp->mutable_payload());
    }
}

void LobbyServiceImpl::HandleCreateRole(const GatewayRequest* req, GatewayResponse* rsp) {
    CreateRoleReq create_req;
    if (!create_req.ParseFromString(req->payload())) {
        return;
    }

    bool success = DBManager::GetInstance().CreatePlayer(req->uid(), create_req.nickname());

    CreateRoleRsp create_rsp;
    create_rsp.set_err_code(success ? 0 : -1);

    rsp->set_cmd_id(1003);
    create_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleLogout(const GatewayRequest* req, GatewayResponse* rsp) {
    LogoutRsp logout_rsp;
    logout_rsp.set_err_code(0);
    
    rsp->set_cmd_id(1002);
    rsp->set_kick_client(true);
    logout_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleMatchGame(const GatewayRequest* req, GatewayResponse* rsp) {
    MatchGameReq match_req;
    if (!match_req.ParseFromString(req->payload())) {
        return;
    }

    std::cout << "Player " << req->uid() << " queuing with char_id: " << match_req.character_id() << std::endl;

    MatchGameRsp match_rsp;
    match_rsp.set_err_code(0);

    rsp->set_cmd_id(1005);
    match_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleMatchStop(const GatewayRequest* req, GatewayResponse* rsp) {
    MatchStopRsp stop_rsp;
    stop_rsp.set_err_code(0);
    stop_rsp.set_success(true);

    rsp->set_cmd_id(1006);
    stop_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleGetPlayerData(const GatewayRequest* req, GatewayResponse* rsp) {
    auto data = DBManager::GetInstance().GetPlayerData(req->uid());
    
    GetPlayerDataRsp p_rsp;
    if (data) {
        p_rsp.set_err_code(0);
        auto* p_info = p_rsp.mutable_player();
        p_info->set_uid(data->uid);
        p_info->set_nickname(data->nickname);
        p_info->set_data_json(data->data_json);
    } else {
        p_rsp.set_err_code(-1); // Player not found or DB error
    }

    rsp->set_cmd_id(1008);
    p_rsp.SerializeToString(rsp->mutable_payload());
}
