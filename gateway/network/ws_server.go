package network

import (
	"context"
	"encoding/binary"
	"fmt"
	"net/http"

	"github.com/gorilla/websocket"
	"github.com/redis/go-redis/v9"
)

type WSServer struct {
	redisClient *redis.Client
	udpPort     int
	upgrader    websocket.Upgrader
}

func NewWSServer(rdb *redis.Client, udpPort int) *WSServer {
	return &WSServer{
		redisClient: rdb,
		udpPort:     udpPort,
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool {
				return true // Allow all origins for now
			},
		},
	}
}

type AuthResponse struct {
	Code    int    `json:"code"`
	Msg     string `json:"msg"`
	ConnID  uint32 `json:"conn_id,omitempty"`
	Key     uint32 `json:"key,omitempty"`
	UDPPort int    `json:"udp_port,omitempty"`
}

func (ws *WSServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	// 1. Extract Token from URL query parameters
	token := r.URL.Query().Get("token")
	if token == "" {
		http.Error(w, "Missing token", http.StatusUnauthorized)
		return
	}

	// 2. Validate token against Redis
	uid, err := ws.redisClient.Get(context.Background(), "auth:"+token).Result()
	if err != nil || uid == "" {
		http.Error(w, "Invalid token", http.StatusUnauthorized)
		return
	}

	// 3. Upgrade to WebSocket
	conn, err := ws.upgrader.Upgrade(w, r, nil)
	if err != nil {
		fmt.Printf("WebSocket Upgrade error: %v\n", err)
		return
	}

	// 4. Create Session
	if DefaultManager == nil {
		conn.Close()
		return
	}

	sess := DefaultManager.AddSession(token, uid, conn)
	fmt.Printf("User %s connected via WebSocket, Assigned ID: %d, Key: %d\n", uid, sess.ID, sess.Key)

	// 5. Send back AuthResponse & UDP handshake info
	resp := AuthResponse{
		Code:    0,
		Msg:     "Success",
		ConnID:  sess.ID,
		Key:     sess.Key,
		UDPPort: ws.udpPort,
	}
	if err := conn.WriteJSON(resp); err != nil {
		fmt.Printf("Failed to write AuthResponse to WS for ID %d: %v\n", sess.ID, err)
		sess.Close()
		return
	}

	// 6. Start read loop for WebSocket
	go ws.wsReadLoop(sess)
}

func (ws *WSServer) wsReadLoop(sess *Session) {
	defer sess.Close()
	for {
		messageType, data, err := sess.WSConn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				fmt.Printf("WebSocket error for ID %d: %v\n", sess.ID, err)
			} else {
				fmt.Printf("WebSocket connection closed for ID %d\n", sess.ID)
			}
			break
		}

		if messageType != websocket.BinaryMessage {
			fmt.Printf("Warning: Received non-binary message type from ID %d\n", sess.ID)
			continue
		}

		if len(data) < 2 {
			fmt.Printf("Warning: Received payload too short (< 2 bytes) from ID %d\n", sess.ID)
			continue
		}

		// Extract CmdID (first 2 bytes) and Payload (remaining)
		cmdID := binary.BigEndian.Uint16(data[0:2])
		payload := data[2:]

		sess.DispatchPacket(cmdID, payload)
	}
}
