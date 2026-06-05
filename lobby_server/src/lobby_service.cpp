#include "lobby_service.h"
#include "db_manager.h"
#include "err_code.h"
#include <iostream>
#include <chrono>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <nlohmann/json.hpp>

using grpc::Status;
using grpc::ServerContext;
using namespace kihan::internal;
using namespace kihan::api;

LobbyServiceImpl::LobbyServiceImpl() {
    // TODO: read game server address from config instead of hardcoding
    auto channel = grpc::CreateChannel("127.0.0.1:50052", grpc::InsecureChannelCredentials());
    game_stub_ = GameControlService::NewStub(channel);

    running_ = true;
}

LobbyServiceImpl::~LobbyServiceImpl() {
    running_ = false;
}

Status LobbyServiceImpl::Subscribe(grpc::ServerContext* context, 
                                   const kihan::internal::Empty* request, 
                                   grpc::ServerWriter<kihan::internal::GatewayResponse>* writer) {
    std::cout << "[Lobby] Gateway connected to Push Stream" << std::endl;
    {
        std::lock_guard<std::mutex> lock(push_mutex_);
        push_writer_ = writer;
    }

    // Keep the stream alive until Gateway disconnects or Lobby shutting down
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (context->IsCancelled()) break;
    }

    {
        std::lock_guard<std::mutex> lock(push_mutex_);
        push_writer_ = nullptr;
    }
    std::cout << "[Lobby] Gateway disconnected from Push Stream" << std::endl;
    return Status::OK;
}

void LobbyServiceImpl::PushToGateway(uint32_t conn_id, uint32_t cmd_id, const std::string& payload) {
    std::lock_guard<std::mutex> lock(push_mutex_);
    if (push_writer_) {
        GatewayResponse rsp;
        rsp.set_conn_id(conn_id);
        rsp.set_cmd_id(cmd_id);
        rsp.set_payload(payload);
        push_writer_->Write(rsp);
    }
}

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
    
    {
        std::lock_guard<std::mutex> match_lock(match_mutex_);
        for (auto it = match_queue_.begin(); it != match_queue_.end();) {
            if (it->uid == request->uid()) {
                it = match_queue_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
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

    auto data = DBManager::GetInstance().GetPlayerData(req->uid());
    std::string nickname = data ? data->nickname : "Player";

    std::cout << "Player " << req->uid() << " queuing with char_id: " << match_req.character_id() << std::endl;

    MatchPlayer p1, p2;
    bool matched = false;

    {
        std::lock_guard<std::mutex> lock(match_mutex_);
        // check if already in queue
        bool in_queue = false;
        for (const auto& p : match_queue_) {
            if (p.uid == req->uid()) {
                in_queue = true;
                break;
            }
        }
        if (!in_queue) {
            match_queue_.push_back({req->uid(), req->conn_id(), match_req.character_id(), nickname});
        }

        if (match_queue_.size() >= 2) {
            p1 = match_queue_.front();
            match_queue_.pop_front();
            p2 = match_queue_.front();
            match_queue_.pop_front();
            matched = true;
        }
    }

    MatchGameRsp match_rsp;
    match_rsp.set_err_code(LOBBY_ERR_OK);

    rsp->set_cmd_id(1005);
    match_rsp.SerializeToString(rsp->mutable_payload());

    if (matched) {
        std::cout << "[Lobby] Matched " << p1.uid << " and " << p2.uid << ". Requesting GameServer..." << std::endl;

        // Prepare player list JSON
        nlohmann::json players = nlohmann::json::array();
        players.push_back({{"uid", std::stoul(p1.uid)}, {"nickname", p1.nickname}, {"character_id", p1.character_id}});
        players.push_back({{"uid", std::stoul(p2.uid)}, {"nickname", p2.nickname}, {"character_id", p2.character_id}});

        CreateRoomReq room_req;
        room_req.set_player_list_json(players.dump());

        CreateRoomRsp room_rsp;
        grpc::ClientContext context;
        Status status = game_stub_->CreateRoom(&context, room_req, &room_rsp);

        if (status.ok() && room_rsp.err_code() == 0) {
            std::cout << "[Lobby] GameRoom created! ID=" << room_rsp.room_id() << std::endl;

            MatchGameNtf ntf;
            ntf.set_err_code(0);
            ntf.set_room_id(std::to_string(room_rsp.room_id()));
            ntf.set_room_snapshot_json(players.dump());

            ntf.set_position(1);
            std::string p1_payload;
            ntf.SerializeToString(&p1_payload);
            PushToGateway(p1.conn_id, 1007, p1_payload);

            ntf.set_position(2);
            std::string p2_payload;
            ntf.SerializeToString(&p2_payload);
            PushToGateway(p2.conn_id, 1007, p2_payload);
        } else {
            std::cerr << "[Lobby] Failed to create room on GameServer. Status=" 
                      << status.error_code() << ": " << status.error_message() << std::endl;
            
            MatchGameNtf ntf;
            ntf.set_err_code(-1);
            std::string err_payload;
            ntf.SerializeToString(&err_payload);
            PushToGateway(p1.conn_id, 1007, err_payload);
            PushToGateway(p2.conn_id, 1007, err_payload);
        }
    }
}

void LobbyServiceImpl::HandleMatchStop(const GatewayRequest* req, GatewayResponse* rsp) {
    {
        std::lock_guard<std::mutex> lock(match_mutex_);
        for (auto it = match_queue_.begin(); it != match_queue_.end();) {
            if (it->uid == req->uid()) {
                it = match_queue_.erase(it);
            } else {
                ++it;
            }
        }
    }

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