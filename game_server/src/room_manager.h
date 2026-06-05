#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include "room.h"

// Forward declaration for the gRPC stream writer wrapper
class SessionStream;

class RoomManager {
public:
    static RoomManager& GetInstance() {
        static RoomManager instance;
        return instance;
    }

    void RunLoop();
    void Stop();

    std::shared_ptr<Room> GetOrCreateRoom(int32_t room_id);
    std::shared_ptr<Room> GetRoom(int32_t room_id);
    void RemoveRoom(int32_t room_id);

    void RegisterSession(uint32_t uid, std::shared_ptr<SessionStream> stream);
    void UnregisterSession(uint32_t uid);
    
    void BroadcastToRoom(int32_t room_id, int32_t cmd_id, const std::string& payload);
    void SendToPlayer(uint32_t uid, int32_t cmd_id, const std::string& payload);

    std::shared_ptr<Room> GetRoomByUid(uint32_t uid);
    void MapUidToRoom(uint32_t uid, int32_t room_id);
    void UnmapUid(uint32_t uid);

private:
    RoomManager() = default;
    ~RoomManager() = default;

    std::unordered_map<int32_t, std::shared_ptr<Room>> rooms_;
    std::mutex rooms_mutex_;

    std::unordered_map<uint32_t, int32_t> uid_to_room_;
    std::mutex mapping_mutex_;

    std::unordered_map<uint32_t, std::shared_ptr<SessionStream>> sessions_;
    std::mutex sessions_mutex_;

    std::atomic<bool> running_{false};
};

class SessionStream {
public:
    virtual ~SessionStream() = default;
    virtual void Send(int32_t cmd_id, const std::string& payload) = 0;
};