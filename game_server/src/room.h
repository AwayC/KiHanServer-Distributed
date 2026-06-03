#pragma once
#include <vector>
#include <unordered_map>
#include <mutex>
#include "player.h"
#include "client_game.pb.h"

enum class RoomState {
    IDLE,
    GAMING
};

class Room {
public:
    Room(int32_t id);
    
    int32_t GetId() const { return id_; }
    RoomState GetState() const { return state_; }
    
    void AddPlayer(const std::string& uid, const std::string& nickname, int32_t char_id);
    void RemovePlayer(const std::string& uid);
    bool HasPlayer(const std::string& uid) const;
    bool IsFull() const;
    
    Player* GetPlayer(const std::string& uid);
    std::vector<std::string> GetPlayerUids() const;
    
    int32_t AssignGameId();
    
    void PushInput(const std::string& uid, const kihan::api::InputFrame& input);
    void Tick(); // Called every 66ms
    
    bool CheckAllReady();
    void StartGame();
    void StopGame(const std::string& winner_uid);
    
    std::string BuildSnapshotJson();
    
private:
    void Broadcast(int32_t cmd_id, const std::string& payload);

    int32_t id_;
    RoomState state_ = RoomState::IDLE;
    
    std::vector<std::string> player_uids_;
    std::unordered_map<std::string, Player> players_;
    
    kihan::api::RoomFrame current_frame_;
    mutable std::mutex room_mutex_;
};