#include "db_manager.h"

bool DBManager::Init(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbname) {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        std::cerr << "mysql_init failed" << std::endl;
        return false;
    }

    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, 0)) {
        std::cerr << "mysql_real_connect failed: " << mysql_error(conn_) << std::endl;
        return false;
    }

    std::string create_table_query = R"(
        CREATE TABLE IF NOT EXISTS kihan_game_players (
            uid VARCHAR(64) PRIMARY KEY,
            nickname VARCHAR(64) NOT NULL UNIQUE,
            create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
            data JSON
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )";

    if (mysql_query(conn_, create_table_query.c_str())) {
        std::cerr << "Failed to create table: " << mysql_error(conn_) << std::endl;
        return false;
    }

    std::cout << "Database initialized successfully." << std::endl;
    return true;
}

bool DBManager::PlayerExists(const std::string& uid) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    std::string query = "SELECT 1 FROM kihan_game_players WHERE uid = '" + uid + "' LIMIT 1";
    if (mysql_query(conn_, query.c_str())) {
        std::cerr << "PlayerExists query failed: " << mysql_error(conn_) << std::endl;
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) return false;

    bool exists = (mysql_num_rows(res) > 0);
    mysql_free_result(res);
    return exists;
}

bool DBManager::CreatePlayer(const std::string& uid, const std::string& nickname) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    // In a real app, bind parameters to prevent SQL injection.
    // Assuming simple validation for now.
    std::string query = "INSERT INTO kihan_game_players (uid, nickname, data) VALUES ('" + 
                        uid + "', '" + nickname + "', '{}')";
                        
    if (mysql_query(conn_, query.c_str())) {
        std::cerr << "CreatePlayer failed: " << mysql_error(conn_) << std::endl;
        return false;
    }

    return true;
}
