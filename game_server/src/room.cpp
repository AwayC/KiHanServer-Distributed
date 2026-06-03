#include "room.h"
#include "room_manager.h"
#include <iostream>

Room::Room(int32_t id) : id_(id) {
    current_frame_.set_frame_id(0);
}

void Room::AddPlayer(const std::string& uid, const std::string& nickname, int32_t char_id) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (players_.find(uid) == players_.end()) {
        player_uids_.push_back(uid);
        players_.emplace(uid, Player(uid, nickname, char_id));
    }
}

void Room::RemovePlayer(const std::string& uid) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    players_.erase(uid);
    for (auto it = player_uids_.begin(); it != player_uids_.end(); ++it) {
        if (*it == uid) {
            player_uids_.erase(it);
            break;
        }
    }
}

bool Room::HasPlayer(const std::string& uid) const {
    return players_.find(uid) != players_.end();
}

bool Room::IsFull() const {
    return player_uids_.size() >= 2;
}

Player* Room::GetPlayer(const std::string& uid) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    auto it = players_.find(uid);
    if (it != players_.end()) return &(it->second);
    return nullptr;
}

std::vector<std::string> Room::GetPlayerUids() const {
    std::lock_guard<std::mutex> lock(room_mutex_);
    return player_uids_;
}

int32_t Room::AssignGameId() {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (player_uids_.empty()) return 1;
    if (players_.at(player_uids_[0]).game_id == 1) return 2;
    return 1;
}

void Room::PushInput(const std::string& uid, const kihan::api::InputFrame& input) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (state_ != RoomState::GAMING) return;
    
    auto it = players_.find(uid);
    if (it == players_.end()) return;
    
    int32_t game_id = it->second.game_id;
    
    // Ignore older frames
    if (input.frame_id() < current_frame_.frame_id()) return;
    
    // Store input for current frame. If already exists, ignore (first come first served for same frame)
    auto& inputs_map = *current_frame_.mutable_inputs();
    if (inputs_map.find(game_id) == inputs_map.end()) {
        inputs_map[game_id] = input;
    }
}

void Room::Tick() {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (state_ != RoomState::GAMING) return;

    current_frame_.set_player_count(player_uids_.size());

    // Serialize and broadcast RoomFrameUpdate
    kihan::api::RoomFrameUpdate update;
    *update.mutable_frame() = current_frame_;
    
    std::string payload;
    update.SerializeToString(&payload);
    Broadcast(2005, payload); // 2005: RoomFrameUpdate

    // Prepare next frame
    current_frame_.set_frame_id(current_frame_.frame_id() + 1);
    current_frame_.mutable_inputs()->clear();
}

bool Room::CheckAllReady() {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (player_uids_.size() < 2) return false;
    for (const auto& pair : players_) {
        if (!pair.second.ready) return false;
    }
    return true;
}

void Room::StartGame() {
    std::lock_guard<std::mutex> lock(room_mutex_);
    state_ = RoomState::GAMING;
    current_frame_.set_frame_id(0);
    current_frame_.mutable_inputs()->clear();
    
    kihan::api::GameStartNtf ntf;
    ntf.set_room_id(id_);
    std::string payload;
    ntf.SerializeToString(&payload);
    Broadcast(2003, payload); // 2003: GameStartNtf
}

void Room::StopGame(const std::string& winner_uid) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    state_ = RoomState::IDLE;
    for (auto& pair : players_) {
        pair.second.ready = false;
    }
    
    kihan::api::GameOverNtf ntf;
    ntf.set_room_id(id_);
    ntf.set_winner_uid(winner_uid);
    std::string payload;
    ntf.SerializeToString(&payload);
    Broadcast(2007, payload); // 2007: GameOverNtf
}

std::string Room::BuildSnapshotJson() {
    std::lock_guard<std::mutex> lock(room_mutex_);
    std::string json = "[";
    bool first = true;
    for (const auto& uid : player_uids_) {
        const auto& p = players_.at(uid);
        if (!first) json += ",";
        json += "{\"uid\":\"" + p.uid + "\",\"nickname\":\"" + p.nickname + "\",\"character_id\":" + std::to_string(p.character_id) + "}";
        first = false;
    }
    json += "]";
    return json;
}

void Room::Broadcast(int32_t cmd_id, const std::string& payload) {
    RoomManager::GetInstance().BroadcastToRoom(id_, cmd_id, payload);
}