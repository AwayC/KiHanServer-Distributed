#pragma once

/**
 * LobbyServer Error Codes
 * Follows the style of login_server (negative values for errors, 0 for OK)
 * Range: -2000 to -2999
 */
enum LobbyErrCode {
    LOBBY_ERR_OK = 0,

    LOBBY_ERR_API_BAD_REQ        = -2000,
    LOBBY_ERR_API_INTERNAL_ERROR = -2001,

    LOBBY_ERR_API_DB_ERROR       = -2200,
    LOBBY_ERR_PLAYER_EXISTS      = -2201,
    LOBBY_ERR_PLAYER_NOT_EXISTS  = -2202,

    LOBBY_ERR_MATCH_FAILED       = -2300,
};
