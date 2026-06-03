#include "game_service.h"
#include "client_game.pb.h"
#include <iostream>

static std::atomic<int32_t> next_room_id{1000};

// --- GameControlService (Lobby -> Game) ---

grpc::Status GameControlServiceImpl::CreateRoom(grpc::ServerContext* context, 
                                                const kihan::internal::CreateRoomReq* request, 
                                                kihan::internal::CreateRoomRsp* response) {
    int32_t room_id = next_room_id.fetch_add(1);
    
    auto room = RoomManager::GetInstance().GetOrCreateRoom(room_id);
    
    // Add players to room
    if (request->has_p1()) {
        room->AddPlayer(request->p1().uid(), request->p1().nickname(), request->p1().character_id());
    }
    if (request->has_p2()) {
        room->AddPlayer(request->p2().uid(), request->p2().nickname(), request->p2().character_id());
    }

    std::cout << "[Game] Created Room " << room_id << " for players." << std::endl;

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
        std::string uid = req.uid();
        uint32_t conn_id = req.conn_id();
        
        std::cout << "[Game] Client Stream established: UID=" << uid << ", ConnID=" << conn_id << std::endl;
        
        auto session_stream = std::make_shared<GrpcSessionStream>(stream, conn_id);
        RoomManager::GetInstance().RegisterSession(uid, session_stream);
        
        // Process the first request
        auto process_req = [&](const kihan::internal::GatewayRequest& request) {
            uint32_t cmd_id = request.cmd_id();
            std::string cur_uid = request.uid();
            const std::string& payload = request.payload();
            
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
                        if (player->game_id == 0) {
                            player->game_id = room->AssignGameId();
                        }
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
                kihan::api::PlayerFrameInput input_req;
                if (input_req.ParseFromString(payload)) {
                    auto room = RoomManager::GetInstance().GetRoomByUid(cur_uid);
                    if (room) {
                        room->PushInput(cur_uid, input_req.input());
                    }
                }
            }
            else if (cmd_id == 2006) { // GameOverReq
                kihan::api::GameOverReq over_req;
                if (over_req.ParseFromString(payload)) {
                    auto room = RoomManager::GetInstance().GetRoomByUid(cur_uid);
                    kihan::api::GameOverRsp rsp;
                    if (room) {
                        room->StopGame(over_req.winner_uid());
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
                std::string winner = "";
                for (const auto& r_uid : room->GetPlayerUids()) {
                    if (r_uid != uid) {
                        winner = r_uid;
                        break;
                    }
                }
                room->StopGame(winner);
            }
        }
        
        RoomManager::GetInstance().UnmapUid(uid);
        RoomManager::GetInstance().UnregisterSession(uid);
    }
    
    return grpc::Status::OK;
}