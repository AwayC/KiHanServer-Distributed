#include "room_manager.h"
#include <thread>
#include <chrono>
#include <iostream>

const int POLL_DURATION = 2;

void RoomManager::RunLoop() {
    running_ = true;
    auto next_tick = std::chrono::steady_clock::now();
    const auto tick_duration = std::chrono::milliseconds(66); // ~15 FPS

    while (running_) {
        std::vector<std::shared_ptr<Room>> rooms_to_process;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            for (auto& pair : rooms_) {
                rooms_to_process.push_back(pair.second);
            }
        }

        for (auto& room : rooms_to_process) {
            if (room->GetState() == RoomState::GAMING) {
                room->Tick();
            } else if (room->GetState() == RoomState::IDLE) {
                if (room->IsFull() && room->CheckAllReady()) {
                    std::cout << "[RoomManager] Room " << room->GetId() << " is starting." << std::endl;
                    room->StartGame();
                }
            }
        }

        next_tick += tick_duration;
        std::this_thread::sleep_until(next_tick);
    }
}

void RoomManager::Stop() {
    running_ = false;
}

std::shared_ptr<Room> RoomManager::GetOrCreateRoom(int32_t room_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }
    auto room = std::make_shared<Room>(room_id);
    rooms_[room_id] = room;
    return room;
}

std::shared_ptr<Room> RoomManager::GetRoom(int32_t room_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }
    return nullptr;
}

void RoomManager::RemoveRoom(int32_t room_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    rooms_.erase(room_id);
}

void RoomManager::DestroyRoom(int32_t room_id) {
    auto room = GetRoom(room_id);
    if (!room) return;

    // Unmap all players
    auto uids = room->GetPlayerUids();
    for (uint32_t uid : uids) {
        UnmapUid(uid);
    }

    // Remove room
    RemoveRoom(room_id);
    std::cout << "[RoomManager] Destroyed Room " << room_id << std::endl;
}

void RoomManager::RegisterSession(uint32_t uid, std::shared_ptr<SessionStream> stream) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[uid] = stream;
}

void RoomManager::UnregisterSession(uint32_t uid) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(uid);
}

void RoomManager::SendToPlayers(const std::vector<uint32_t>& uids, int32_t cmd_id, const std::string& payload) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (uint32_t uid : uids) {
        auto it = sessions_.find(uid);
        if (it != sessions_.end()) {
            it->second->Send(cmd_id, payload);
        }
    }
}

void RoomManager::SendToPlayer(uint32_t uid, int32_t cmd_id, const std::string& payload) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(uid);
    if (it != sessions_.end()) {
        it->second->Send(cmd_id, payload);
    }
}

std::shared_ptr<Room> RoomManager::GetRoomByUid(uint32_t uid) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    auto it = uid_to_room_.find(uid);
    if (it != uid_to_room_.end()) {
        return GetRoom(it->second);
    }
    return nullptr;
}

void RoomManager::MapUidToRoom(uint32_t uid, int32_t room_id) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    uid_to_room_[uid] = room_id;
}

void RoomManager::UnmapUid(uint32_t uid) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    uid_to_room_.erase(uid);
}