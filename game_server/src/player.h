#pragma once
#include <string>

class Player {
public:
    uint32_t uid;
    std::string nickname;
    int32_t character_id = 0;
    int32_t game_id = 0; // 1P or 2P
    bool ready = false;
    bool offline = false;

    Player(uint32_t u, const std::string& n, int32_t c) 
        : uid(u), nickname(n), character_id(c) {}
};