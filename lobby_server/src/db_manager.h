#pragma once

#include <string>
#include <mysqlx/xdevapi.h>
#include <mutex>
#include <memory>
#include <iostream>

struct PlayerData {
    uint32_t uid;
    std::string nickname;
    std::string data_json;
};

class DBManager {
public:
    static DBManager& GetInstance() {
        static DBManager instance;
        return instance;
    }

    bool Init(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbname);
    
    // Check if player exists
    bool PlayerExists(uint32_t uid);
    
    // Create new player
    bool CreatePlayer(uint32_t uid, const std::string& nickname);

    // Fetch full player data
    std::unique_ptr<PlayerData> GetPlayerData(uint32_t uid);

    // Update battle statistics in JSON data field
    bool UpdateBattleStats(uint32_t uid, bool is_win);

private:
    DBManager() = default;
    ~DBManager() = default;

    std::string valToString(const mysqlx::Value& val);

    std::unique_ptr<mysqlx::Session> session_;
    std::string db_name_;
    std::mutex db_mutex_;
};
