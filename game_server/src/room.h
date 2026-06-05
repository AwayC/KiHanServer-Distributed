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
    
    void AddPlayer(uint32_t uid, const std::string& nickname, int32_t char_id);
    void RemovePlayer(uint32_t uid);
    bool HasPlayer(uint32_t uid) const;
    bool IsFull() const;
    
    Player* GetPlayer(uint32_t uid);
    std::vector<uint32_t> GetPlayerUids() const;
    
    int32_t AssignGameId();
    
    void PushInput(uint32_t uid, const std::string& raw_input);
    void Tick(); // Called every 66ms
    
    bool CheckAllReady();
    void StartGame();
    void StopGame(uint32_t winner_uid);
    
    std::string BuildSnapshotJson();
    
private:
    void Broadcast(int32_t cmd_id, const std::string& payload);

    int32_t id_;
    RoomState state_ = RoomState::IDLE;
    
    std::vector<uint32_t> player_uids_;
    std::unordered_map<uint32_t, Player> players_;
    
    kihan::api::RoomFrame current_frame_;
    mutable std::mutex room_mutex_;
};