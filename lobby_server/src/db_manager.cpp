#include "db_manager.h"
#include <sstream>

bool DBManager::Init(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbname) {
    try {
        db_name_ = dbname;
        std::cout << "[DB] Connecting to MySQL at " << host << ":" << port << "..." << std::endl;
        // Connect without a default schema first to ensure connection works
        session_ = std::make_unique<mysqlx::Session>(host, port, user, password);
        
        // Create database if not exists
        session_->sql("CREATE DATABASE IF NOT EXISTS " + dbname).execute();
        // Switch to the database
        session_->sql("USE " + dbname).execute();
        
        // Create table if not exists using SQL execute
        session_->sql(R"(
            CREATE TABLE IF NOT EXISTS kihan_game_players (
                uid VARCHAR(64) PRIMARY KEY,
                nickname VARCHAR(64) NOT NULL UNIQUE,
                create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
                data JSON
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
        )").execute();

        std::cout << "[DB] Database and table initialized successfully." << std::endl;
        return true;
    } catch (const mysqlx::Error &err) {
        std::cerr << "[DB] Database Init Error: " << err.what() << std::endl;
        return false;
    } catch (const std::exception &ex) {
        std::cerr << "[DB] Database Init Exception: " << ex.what() << std::endl;
        return false;
    }
}

bool DBManager::PlayerExists(uint32_t uid) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!session_) return false;
    try {
        auto schema = session_->getSchema(db_name_);
        auto table = schema.getTable("kihan_game_players");
        auto res = table.select("1").where("uid = :uid").bind("uid", std::to_string(uid)).execute();
        return res.count() > 0;
    } catch (const mysqlx::Error &err) {
        std::cerr << "PlayerExists Error: " << err.what() << std::endl;
        return false;
    }
}

bool DBManager::CreatePlayer(uint32_t uid, const std::string& nickname) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!session_) return false;
    try {
        session_->sql("USE " + db_name_).execute();
        
        // Use INSERT ... ON DUPLICATE KEY UPDATE to allow setting nickname 
        // if a record with this UID already exists but was incomplete.
        // Also initialize battle stats here.
        session_->sql(R"(
            INSERT INTO kihan_game_players (uid, nickname, data) 
            VALUES (?, ?, ?) 
            ON DUPLICATE KEY UPDATE nickname = VALUES(nickname), data = VALUES(data)
        )")
        .bind(std::to_string(uid))
        .bind(nickname)
        .bind(R"({"total_battle_count": 0, "win_count": 0})")
        .execute();
        
        return true;
    } catch (const mysqlx::Error &err) {
        std::cerr << "CreatePlayer Error: " << err.what() << std::endl;
        return false;
    }
}

std::string DBManager::valToString(const mysqlx::Value& val) {
    if (val.isNull()) return "";
    std::stringstream ss;
    ss << val;
    std::string s = ss.str();
    
    // X DevAPI stringstream for strings might wrap them in double quotes
    // if they are not already JSON. We want the raw string for uid/nickname.
    // However, for JSON objects, we WANT the quotes/structure.
    // A simple check: if it starts and ends with ", and it's a string type.
    if (val.getType() == mysqlx::Value::STRING && s.length() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.length() - 2);
    }
    return s;
}

std::unique_ptr<PlayerData> DBManager::GetPlayerData(uint32_t uid) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!session_) return nullptr;
    try {
        auto schema = session_->getSchema(db_name_);
        auto table = schema.getTable("kihan_game_players");
        auto res = table.select("uid", "nickname", "data")
                        .where("uid = :uid")
                        .bind("uid", uid)
                        .execute();
        
        auto row = res.fetchOne();
        if (!row) return nullptr;

        auto data = std::make_unique<PlayerData>();
        data->uid = uid;
        data->nickname = valToString(row[1]);
        
        if (!row[2].isNull()) {
            data->data_json = valToString(row[2]);
        } else {
            data->data_json = "{}";
        }

        return data;
    } catch (const mysqlx::Error &err) {
        std::cerr << "GetPlayerData Error: " << err.what() << std::endl;
        return nullptr;
    }
}

bool DBManager::UpdateBattleStats(uint32_t uid, bool is_win) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!session_) return false;
    try {
        // Switch to the correct schema
        session_->sql("USE " + db_name_).execute();
        
        // Use SQL to atomically update JSON fields
        int win_inc = is_win ? 1 : 0;
        
        session_->sql(R"(
            UPDATE kihan_game_players 
            SET data = JSON_SET(
                IFNULL(data, '{}'), 
                '$.total_battle_count', CAST(IFNULL(JSON_EXTRACT(data, '$.total_battle_count'), 0) AS UNSIGNED) + 1,
                '$.win_count', CAST(IFNULL(JSON_EXTRACT(data, '$.win_count'), 0) AS UNSIGNED) + ?
            ) 
            WHERE uid = ?;
        )").bind(win_inc).bind(uid).execute();
        
        return true;
    } catch (const mysqlx::Error &err) {
        std::cerr << "UpdateBattleStats Error: " << err.what() << std::endl;
        return false;
    }
}