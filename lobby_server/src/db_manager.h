#pragma once

#include <string>
#include <mysqlx/xdevapi.h>
#include <mutex>
#include <memory>
#include <iostream>

struct PlayerData {
    std::string uid;
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
    bool PlayerExists(const std::string& uid);
    
    // Create new player
    bool CreatePlayer(const std::string& uid, const std::string& nickname);

    // Fetch full player data
    std::unique_ptr<PlayerData> GetPlayerData(const std::string& uid);

private:
    DBManager() = default;
    ~DBManager() = default;

    std::unique_ptr<mysqlx::Session> session_;
    std::mutex db_mutex_;
};
