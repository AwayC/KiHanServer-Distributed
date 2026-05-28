#pragma once

#include <string>
#include <mysql.h>
#include <mutex>
#include <iostream>

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

private:
    DBManager() : conn_(nullptr) {}
    ~DBManager() {
        if (conn_) {
            mysql_close(conn_);
        }
    }

    MYSQL* conn_;
    std::mutex db_mutex_;
};
