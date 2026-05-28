#include "db_manager.h"

bool DBManager::Init(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbname) {
    try {
        // X DevAPI uses a session-based approach
        session_ = std::make_unique<mysqlx::Session>(host, port, user, password, dbname);
        
        // Create table if not exists using SQL execute
        session_->sql(R"(
            CREATE TABLE IF NOT EXISTS kihan_game_players (
                uid VARCHAR(64) PRIMARY KEY,
                nickname VARCHAR(64) NOT NULL UNIQUE,
                create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
                data JSON
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
        )").execute();

        std::cout << "Database initialized successfully (X DevAPI)." << std::endl;
        return true;
    } catch (const mysqlx::Error &err) {
        std::cerr << "Database Init Error: " << err.what() << std::endl;
        return false;
    } catch (const std::exception &ex) {
        std::cerr << "Database Init Exception: " << ex.what() << std::endl;
        return false;
    }
}

bool DBManager::PlayerExists(const std::string& uid) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    try {
        auto table = session_->getSchema("kihan_db").getTable("kihan_game_players");
        auto res = table.select("1").where("uid = :uid").bind("uid", uid).execute();
        return res.count() > 0;
    } catch (const mysqlx::Error &err) {
        std::cerr << "PlayerExists Error: " << err.what() << std::endl;
        return false;
    }
}

bool DBManager::CreatePlayer(const std::string& uid, const std::string& nickname) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    try {
        auto table = session_->getSchema("kihan_db").getTable("kihan_game_players");
        table.insert("uid", "nickname", "data")
             .values(uid, nickname, "{}")
             .execute();
        return true;
    } catch (const mysqlx::Error &err) {
        std::cerr << "CreatePlayer Error: " << err.what() << std::endl;
        return false;
    }
}

std::unique_ptr<PlayerData> DBManager::GetPlayerData(const std::string& uid) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    try {
        auto table = session_->getSchema("kihan_db").getTable("kihan_game_players");
        auto res = table.select("uid", "nickname", "data")
                        .where("uid = :uid")
                        .bind("uid", uid)
                        .execute();
        
        auto row = res.fetchOne();
        if (!row) return nullptr;

        auto data = std::make_unique<PlayerData>();
        data->uid = (std::string)row[0];
        data->nickname = (std::string)row[1];
        
        // Handle JSON column - X DevAPI might return it as a string or special type
        if (!row[2].isNull()) {
            data->data_json = (std::string)row[2];
        } else {
            data->data_json = "{}";
        }

        return data;
    } catch (const mysqlx::Error &err) {
        std::cerr << "GetPlayerData Error: " << err.what() << std::endl;
        return nullptr;
    }
}
