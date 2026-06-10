#include "game_service.h"
#include "client_game.pb.h"
#include <iostream>
#include <nlohmann/json.hpp>

static std::atomic<int32_t> next_room_id{1000};

// --- GameControlService (Lobby -> Game) ---

grpc::Status GameControlServiceImpl::CreateRoom(grpc::ServerContext* context, 
                                                const kihan::internal::CreateRoomReq* request, 
                                                kihan::internal::CreateRoomRsp* response) {
    int32_t room_id = next_room_id.fetch_add(1);
    // Simple wrap around to prevent eventual overflow to negative numbers
    if (room_id > 1000000000) {
        next_room_id.store(1000);
    }
    
    auto room = RoomManager::GetInstance().GetOrCreateRoom(room_id);
    
    try {
        auto players = nlohmann::json::parse(request->player_list_json());
        for (const auto& p : players) {
            uint32_t uid = p["uid"].is_string() ? std::stoul(p["uid"].get<std::string>()) : p["uid"].get<uint32_t>();
            std::string nickname = p.value("nickname", "");
            int32_t char_id = p.value("character_id", 0);
            room->AddPlayer(uid, nickname, char_id);
        }
    } catch (const std::exception& e) {
        std::cerr << "[Game] Failed to parse player_list_json: " << e.what() << std::endl;
        response->set_err_code(-1);
        return grpc::Status::OK;
    }

    std::cout << "[Game] Created Room " << room_id << " for players. Sending response..." << std::endl;

    response->set_err_code(0); // GAME_ERR_OK
    response->set_room_id(room_id);
    return grpc::Status::OK;
}

// --- GameService (Client <-> Gateway <-> Game) ---

grpc::Status GameServiceImpl::StreamBattle(grpc::ServerContext* context, 
                                           grpc::ServerReaderWriter<kihan::internal::GatewayResponse, kihan::internal::GatewayRequest>* stream) {
    kihan::internal::GatewayRequest req;
    
    // Read the first request to identify the session and bind the stream
    if (stream->Read(&req)) {
        uint32_t uid = req.uid();
        uint32_t conn_id = req.conn_id();
        
        std::cout << "[Game] Client Stream established: UID=" << uid << ", ConnID=" << conn_id << std::endl;
        
        auto session_stream = std::make_shared<GrpcSessionStream>(stream, conn_id);
        RoomManager::GetInstance().RegisterSession(uid, session_stream);
        
        // Process the first request
        auto process_req = [&](const kihan::internal::GatewayRequest& request) {
            uint32_t cmd_id = request.cmd_id();
            uint32_t cur_uid = request.uid();
            const std::string& payload = request.payload();
            
            std::cout << "[Game] Handling Request: CmdID=" << cmd_id << ", UID=" << cur_uid << std::endl;

            if (cmd_id == 2001) { // EnterRoomReq
                kihan::api::EnterRoomReq enter_req;
                if (enter_req.ParseFromString(payload)) {
                    int32_t room_id = enter_req.room_id();
                    auto room = RoomManager::GetInstance().GetRoom(room_id);
                    
                    kihan::api::EnterRoomRsp rsp;
                    if (room && room->HasPlayer(cur_uid)) {
                        rsp.set_err_code(0);
                        rsp.mutable_snapshot()->set_room_id(room_id);
                        rsp.mutable_snapshot()->set_player_list_json(room->BuildSnapshotJson());
                        
                        auto player = room->GetPlayer(cur_uid);
                    
                        rsp.set_my_game_id(player->game_id);
                        RoomManager::GetInstance().MapUidToRoom(cur_uid, room_id);
                    } else {
                        rsp.set_err_code(-3001); // GAME_ERR_ROOM_NOT_EXIST
                    }
                    
                    std::string out_payload;
                    rsp.SerializeToString(&out_payload);
                    session_stream->Send(2001, out_payload);
                }
            }
            else if (cmd_id == 2002) { // PlayerReadyReq
                kihan::api::PlayerReadyReq ready_req;
                if (ready_req.ParseFromString(payload)) {
                    auto room = RoomManager::GetInstance().GetRoom(ready_req.room_id());
                    kihan::api::PlayerReadyRsp rsp;
                    if (room && room->HasPlayer(cur_uid)) {
                        room->GetPlayer(cur_uid)->ready = true;
                        rsp.set_err_code(0);
                    } else {
                        rsp.set_err_code(-3001);
                    }
                    std::string out_payload;
                    rsp.SerializeToString(&out_payload);
                    session_stream->Send(2002, out_payload);
                }
            }
            else if (cmd_id == 2004) { // PlayerFrameInput
                // kihan::api::PlayerFrameInput input_req;
                if (payload.size() == 6) {
                    auto room = RoomManager::GetInstance().GetRoomByUid(cur_uid);
                    if (room) {
                        room->PushInput(cur_uid, payload);
                    }
                }
            }
            else if (cmd_id == 2006) { // GameOverReq
                kihan::api::GameOverReq over_req;
                if (over_req.ParseFromString(payload)) {
                    auto room = RoomManager::GetInstance().GetRoomByUid(cur_uid);
                    kihan::api::GameOverRsp rsp;
                    if (room) {
                        int32_t rid = room->GetId();
                        room->StopGame(over_req.winner_uid());
                        RoomManager::GetInstance().DestroyRoom(rid);
                        rsp.set_err_code(0);
                    } else {
                        rsp.set_err_code(-3001);
                    }
                    std::string out_payload;
                    rsp.SerializeToString(&out_payload);
                    session_stream->Send(2006, out_payload);
                }
            }
        };
        
        process_req(req);
        
        // Continue reading from stream
        while (stream->Read(&req)) {
            process_req(req);
        }
        
        std::cout << "[Game] Client Stream disconnected: UID=" << uid << std::endl;
        
        auto room = RoomManager::GetInstance().GetRoomByUid(uid);
        if (room) {
            // If the game is running and someone disconnects, the other player wins
            if (room->GetState() == RoomState::GAMING) {
                uint32_t winner = 0;
                for (uint32_t r_uid : room->GetPlayerUids()) {
                    if (r_uid != uid) {
                        winner = r_uid;
                        break;
                    }
                }
                int32_t rid = room->GetId();
                room->StopGame(winner);
                RoomManager::GetInstance().DestroyRoom(rid);
            }
        }
        
        RoomManager::GetInstance().UnmapUid(uid);
        RoomManager::GetInstance().UnregisterSession(uid);
    }
    
    return grpc::Status::OK;
}
