#include "lobby_service.h"
#include "db_manager.h"
#include "err_code.h"
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
        case 1009: HandleGetOnlineCount(request, response); break;
        default:
            std::cerr << "[Lobby] Unknown cmd_id: " << cmd_id << std::endl;
            break;
    }

    return Status::OK;
}

Status LobbyServiceImpl::ClientDisconnect(ServerContext* context, const GatewayRequest* request, GatewayResponse* response) {
    std::cout << "[Lobby] Client disconnected - uid: " << request->uid() << " conn_id: " << request->conn_id() << std::endl;
    
    std::lock_guard<std::mutex> lock(online_mutex_);
    online_players_.erase(request->uid());
    
    return Status::OK;
}

void LobbyServiceImpl::HandleLogin(const GatewayRequest* req, GatewayResponse* rsp) {
    LoginReq login_req;
    if (!login_req.ParseFromString(req->payload())) {
        std::cerr << "[Lobby] Failed to parse LoginReq for UID: " << req->uid() << std::endl;
        return;
    }

    auto data = DBManager::GetInstance().GetPlayerData(req->uid());
    
    LoginRsp login_rsp;
    // Check if player exists AND has a non-empty nickname
    if (data && !data->nickname.empty()) {
        {
            std::lock_guard<std::mutex> lock(online_mutex_);
            online_players_.insert(req->uid());
        }

        login_rsp.set_err_code(LOBBY_ERR_OK);
        auto* p_info = login_rsp.mutable_player();
        p_info->set_uid(data->uid);
        p_info->set_nickname(data->nickname);
        p_info->set_data_json(data->data_json);
        
        std::cout << "[Lobby] Login Success: UID=" << data->uid << ", Nickname=" << data->nickname << std::endl;
    } else {
        // Explicitly return NOT_EXISTS error code so client knows to show creation panel
        login_rsp.set_err_code(LOBBY_ERR_PLAYER_NOT_EXISTS);
        std::cout << "[Lobby] Login Failed: Player record not found or nickname empty. Sending LOBBY_ERR_PLAYER_NOT_EXISTS to UID=" << req->uid() << std::endl;
    }

    rsp->set_cmd_id(1001); // Always respond with 1001 to match client's request
    login_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleCreateRole(const GatewayRequest* req, GatewayResponse* rsp) {
    CreateRoleReq create_req;
    if (!create_req.ParseFromString(req->payload())) {
        return;
    }

    bool success = DBManager::GetInstance().CreatePlayer(req->uid(), create_req.nickname());

    CreateRoleRsp create_rsp;
    create_rsp.set_err_code(success ? LOBBY_ERR_OK : LOBBY_ERR_PLAYER_EXISTS); // Assuming failure is mostly name taken

    rsp->set_cmd_id(1003);
    create_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleLogout(const GatewayRequest* req, GatewayResponse* rsp) {
    {
        std::lock_guard<std::mutex> lock(online_mutex_);
        online_players_.erase(req->uid());
    }

    LogoutRsp logout_rsp;
    logout_rsp.set_err_code(LOBBY_ERR_OK);
    
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
    match_rsp.set_err_code(LOBBY_ERR_OK);

    rsp->set_cmd_id(1005);
    match_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleMatchStop(const GatewayRequest* req, GatewayResponse* rsp) {
    MatchStopRsp stop_rsp;
    stop_rsp.set_err_code(LOBBY_ERR_OK);
    stop_rsp.set_success(true);

    rsp->set_cmd_id(1006);
    stop_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleGetPlayerData(const GatewayRequest* req, GatewayResponse* rsp) {
    auto data = DBManager::GetInstance().GetPlayerData(req->uid());
    
    GetPlayerDataRsp p_rsp;
    if (data) {
        p_rsp.set_err_code(LOBBY_ERR_OK);
        auto* p_info = p_rsp.mutable_player();
        p_info->set_uid(data->uid);
        p_info->set_nickname(data->nickname);
        p_info->set_data_json(data->data_json);
    } else {
        p_rsp.set_err_code(LOBBY_ERR_PLAYER_NOT_EXISTS);
    }

    rsp->set_cmd_id(1008);
    p_rsp.SerializeToString(rsp->mutable_payload());
}

void LobbyServiceImpl::HandleGetOnlineCount(const GatewayRequest* req, GatewayResponse* rsp) {
    GetOnlineCountRsp count_rsp;
    count_rsp.set_err_code(LOBBY_ERR_OK);
    std::cout << "[Lobby] GetOnlineCount" << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(online_mutex_);
        count_rsp.set_online_count(static_cast<int32_t>(online_players_.size()));
    }

    rsp->set_cmd_id(1009);
    count_rsp.SerializeToString(rsp->mutable_payload());
}
