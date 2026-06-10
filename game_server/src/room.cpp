#include "room.h"
#include "room_manager.h"
#include <iostream>

Room::Room(int32_t id) : id_(id) {
    current_frame_.set_frame_id(0);
}

void Room::AddPlayer(uint32_t uid, const std::string& nickname, int32_t char_id) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (players_.find(uid) == players_.end()) {
        Player new_player(uid, nickname, char_id);
        new_player.game_id = AssignGameId();
        
        player_uids_.push_back(uid);
        players_.emplace(uid, new_player);
    }
}

void Room::RemovePlayer(uint32_t uid) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    players_.erase(uid);
    for (auto it = player_uids_.begin(); it != player_uids_.end(); ++it) {
        if (*it == uid) {
            player_uids_.erase(it);
            break;
        }
    }
}

bool Room::HasPlayer(uint32_t uid) const {
    return players_.find(uid) != players_.end();
}

bool Room::IsFull() const {
    return player_uids_.size() >= 2;
}

Player* Room::GetPlayer(uint32_t uid) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    auto it = players_.find(uid);
    if (it != players_.end()) return &(it->second);
    return nullptr;
}

std::vector<uint32_t> Room::GetPlayerUids() const {
    std::lock_guard<std::mutex> lock(room_mutex_);
    return player_uids_;
}

int32_t Room::AssignGameId() {
    // 线程不安全
    if (player_uids_.empty()) return 1;
    if (players_.at(player_uids_[0]).game_id == 1) return 2;
    return 1;
}

void Room::PushInput(uint32_t uid, const std::string& raw_input) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    if (state_ != RoomState::GAMING) {
        std::cout << "[Room " << id_ << "] Drop Input: Not Gaming" << std::endl;
        return;
    }

    auto it = players_.find(uid);
    if (it == players_.end()) {
        std::cout << "[Room " << id_ << "] Drop Input: Player " << uid << " not found" << std::endl;
        return;
    }

    int32_t game_id = it->second.game_id;

    // Ignore invalid lengths or older frames
    if (raw_input.length() < 4) {
        std::cout << "[Room " << id_ << "] Drop Input: Too short (" << raw_input.length() << ")" << std::endl;
        return;
    }

    uint32_t input_frame_id = (static_cast<uint8_t>(raw_input[0]) << 24) |
                              (static_cast<uint8_t>(raw_input[1]) << 16) |
                              (static_cast<uint8_t>(raw_input[2]) << 8)  |
                              static_cast<uint8_t>(raw_input[3]);

    if (input_frame_id < current_frame_.frame_id()) {
        std::cout << "[Room " << id_ << "] Drop Input: Old Frame (" << input_frame_id << " < " << current_frame_.frame_id() << ")" << std::endl;
        return;
    }

    // Store input for current frame. If already exists, ignore (first come first served for same frame)
    auto& inputs_map = *current_frame_.mutable_raw_inputs();
    if (inputs_map.find(game_id) == inputs_map.end()) {
        // According to proto, we only broadcast the 2-byte action part [Joystick][Buttons]
        // But since the client sent 6 bytes, we take the last part.
        std::string action;
        if (raw_input.length() >= 6) {
            action = raw_input.substr(4, 2);
        } else {
            action = std::string(2, 0); // Empty action
        }
        inputs_map[game_id] = action;
        std::cout << "[Room " << id_ << "] Stored Input for GameID=" << game_id << " (Frame " << input_frame_id << ")" << std::endl;
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
    current_frame_.mutable_raw_inputs()->clear();
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
    current_frame_.mutable_raw_inputs()->clear();
    
    std::cout << "[Room " << id_ << "] Starting Game!" << std::endl;

    kihan::api::GameStartNtf ntf;
    ntf.set_room_id(id_);
    std::string payload;
    ntf.SerializeToString(&payload);
    Broadcast(2003, payload); // 2003: GameStartNtf
}

void Room::StopGame(uint32_t winner_uid) {
    std::lock_guard<std::mutex> lock(room_mutex_);
    state_ = RoomState::IDLE;
    for (auto& pair : players_) {
        pair.second.ready = false;
    }
    
    std::cout << "[Room " << id_ << "] Stopping Game. Winner: " << winner_uid << std::endl;

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
        json += "{\"uid\":" + std::to_string(p.uid) + ",\"nickname\":\"" + p.nickname + "\",\"character_id\":" + std::to_string(p.character_id) + "}";
        first = false;
    }
    json += "]";
    return json;
}

void Room::Broadcast(int32_t cmd_id, const std::string& payload) {
    std::cout << "[Room " << id_ << "] Broadcasting CmdID=" << cmd_id << " to all players." << std::endl;
    RoomManager::GetInstance().SendToPlayers(player_uids_, cmd_id, payload);
}