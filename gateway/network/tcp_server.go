package network

import (
	"context"
	"encoding/json"
	"fmt"
	"net"

	"github.com/redis/go-redis/v9"
)

type TCPServer struct {
	addr        string
	redisClient *redis.Client
	udpPort     int
}

func NewTCPServer(addr string, rdb *redis.Client, udpPort int) *TCPServer {
	return &TCPServer{
		addr:        addr,
		redisClient: rdb,
		udpPort:     udpPort,
	}
}

type AuthRequest struct {
	Token string `json:"token"`
}

type AuthResponse struct {
	Code    int    `json:"code"`
	Msg     string `json:"msg"`
	ConnID  uint32 `json:"conn_id,omitempty"`
	Key     uint32 `json:"key,omitempty"`
	UDPPort int    `json:"udp_port,omitempty"`
}

func (t *TCPServer) Start() error {
	listener, err := net.Listen("tcp", t.addr)
	if err != nil {
		return err
	}
	fmt.Println("TCP Server listening on", t.addr)

	for {
		conn, err := listener.Accept()
		if err != nil {
			fmt.Println("TCP Accept error:", err)
			continue
		}
		go t.handleConnection(conn)
	}
}

func (t *TCPServer) handleConnection(conn net.Conn) {
	// 1. Read first packet (AuthRequest)
	buf := make([]byte, 1024)
	n, err := conn.Read(buf)
	if err != nil {
		conn.Close()
		return
	}

	var req AuthRequest
	if err := json.Unmarshal(buf[:n], &req); err != nil {
		conn.Close()
		return
	}

	// 2. Validate token against Redis
	uid, err := t.redisClient.Get(context.Background(), "auth:"+req.Token).Result()
	if err != nil || uid == "" {
		resp := AuthResponse{Code: -1, Msg: "Invalid token"}
		respBytes, _ := json.Marshal(resp)
		conn.Write(respBytes)
		conn.Close()
		return
	}

	// 3. Create Session
	if DefaultManager == nil {
		conn.Close()
		return
	}
	
	sess := DefaultManager.AddSession(req.Token, uid, conn)
	fmt.Printf("User %s connected via TCP, Assigned ID: %d, Key: %d\n", uid, sess.ID, sess.Key)

	// 4. Send back UDP handshake info
	resp := AuthResponse{
		Code:    0,
		Msg:     "Success",
		ConnID:  sess.ID,
		Key:     sess.Key,
		UDPPort: t.udpPort,
	}
	respBytes, _ := json.Marshal(resp)
	conn.Write(respBytes)

	// 5. Start read loop for TCP
	go t.tcpReadLoop(sess)
}

func (t *TCPServer) tcpReadLoop(sess *Session) {
	defer sess.Close()
	for {
		cmdID, payload, err := ReadAppFrame(sess.TCPConn)
		if err != nil {
			fmt.Printf("TCP connection closed for ID %d: %v\n", sess.ID, err)
			break
		}
		
		sess.DispatchPacket(cmdID, payload)
	}
}
