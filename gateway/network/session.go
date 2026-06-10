package network

import (
	"context"
	"encoding/binary"
	"log"
	"net"
	"sync"
	"time"

	"gateway/pb"
	"gateway/rpc"
	"github.com/gorilla/websocket"
	"github.com/xtaci/kcp-go/v5"
)

type Session struct {
	ID    uint32
	Key   uint32
	Token string
	UID   uint32

	WSConn *websocket.Conn

	GameStream pb.GameService_StreamBattleClient
	GameCancel context.CancelFunc

	mu            sync.RWMutex
	UDPAddr       *net.UDPAddr
	KCPConn       *kcp.UDPSession
	Connected     bool
	LastHeartbeat time.Time
}

func NewSession(id uint32, key uint32, token string, uid uint32, wsConn *websocket.Conn) *Session {
	return &Session{
		ID:            id,
		Key:           key,
		Token:         token,
		UID:           uid,
		WSConn:        wsConn,
		Connected:     true,
		LastHeartbeat: time.Now(),
	}
}

func (s *Session) SetUDPAddr(addr *net.UDPAddr) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.UDPAddr = addr
	s.LastHeartbeat = time.Now()
}

func (s *Session) UpdateHeartbeat() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.LastHeartbeat = time.Now()
}

func (s *Session) SetKCPConn(conn *kcp.UDPSession) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.KCPConn = conn
}

// Channel 0: WebSocket, Channel 1: KCP, Channel 2: Raw UDP
// Note: 'data' here is expected to be [CmdID(2 bytes)][Payload bytes...]
func (s *Session) Send(channel int, data []byte) error {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if !s.Connected {
		return net.ErrClosed
	}

	switch channel {
	case 1:
		if s.KCPConn != nil {
			// For KCP we still need length prefix framing
			// Since 'data' already contains CmdID and Payload, we just prepend the length
			totalLen := uint16(len(data))
			buf := make([]byte, 2+totalLen)
			binary.BigEndian.PutUint16(buf[0:2], totalLen)
			copy(buf[2:], data)
			_, err := s.KCPConn.Write(buf)
			return err
		}
		// Fallback to WebSocket if KCP not ready
		fallthrough
	case 2:
		if s.UDPAddr != nil && DefaultManager != nil && DefaultManager.UDPServer != nil {
			hdr := EncodeHeader(s.ID, s.Key, PacketTypeRawUDP)
			full := append(hdr, data...)
			_, err := DefaultManager.UDPServer.ConnObj.WriteToUDP(full, s.UDPAddr)
			return err
		}
		// Fallback to WebSocket
		fallthrough
	case 0:
		fallthrough
	default:
		// WebSocket doesn't need length framing
		return s.WSConn.WriteMessage(websocket.BinaryMessage, data)
	}
}

func (s *Session) DispatchPacket(cmdID uint16, payload []byte) {
	if rpc.DefaultRPCManager == nil {
		log.Println("RPC Manager not initialized")
		return
	}

	log.Printf("[Session %d] Dispatching Packet: CmdID=%d, UID=%d, PayloadLen=%d\n", s.ID, cmdID, s.UID, len(payload))

	req := &pb.GatewayRequest{
		ConnId:  s.ID,
		Uid:     s.UID,
		CmdId:   uint32(cmdID),
		Payload: payload,
	}

	// Simple routing logic: CmdID < 2000 -> Lobby, else -> Game
	if cmdID < 2000 {
		log.Printf("[Session %d] Routing to Lobby...\n", s.ID)
		resp, err := rpc.DefaultRPCManager.RouteToLobby(req)
		if err != nil {
			log.Printf("[Session %d] Lobby RPC error: %v\n", s.ID, err)
			return
		}
		
		if resp != nil {
			log.Printf("[Session %d] Received Lobby response: CmdID=%d, PayloadLen=%d, Kick=%v\n", s.ID, resp.CmdId, len(resp.Payload), resp.KickClient)
			// Construct frame without length prefix: [CmdID(2)][Payload]
			outFrame := make([]byte, 2+len(resp.Payload))
			binary.BigEndian.PutUint16(outFrame[0:2], uint16(resp.CmdId))
			copy(outFrame[2:], resp.Payload)

			// Try KCP (channel 1), will fallback to WS (channel 0)
			s.Send(1, outFrame)

			if resp.KickClient {
				s.Close()
			}
		} else {
			log.Printf("[Session %d] Lobby returned empty response\n", s.ID)
		}
	} else {
		// Route to Game Server
		s.mu.Lock()
		if s.GameStream == nil {
			ctx, cancel := context.WithCancel(context.Background())
			s.GameCancel = cancel
			stream, err := rpc.DefaultRPCManager.GetGameClient().StreamBattle(ctx)
			if err != nil {
				s.mu.Unlock()
				log.Printf("[Session %d] Failed to connect to GameServer: %v\n", s.ID, err)
				return
			}
			s.GameStream = stream
			log.Printf("[Session %d] Established GameServer StreamBattle\n", s.ID)

			// Start receive loop for this game stream
			go s.gameStreamRecvLoop(stream)
		}
		stream := s.GameStream
		s.mu.Unlock()

		err := stream.Send(req)
		if err != nil {
			log.Printf("[Session %d] Game stream send error: %v\n", s.ID, err)
		}
	}
}

func (s *Session) gameStreamRecvLoop(stream pb.GameService_StreamBattleClient) {
	for {
		resp, err := stream.Recv()
		if err != nil {
			log.Printf("[Session %d] Game stream closed: %v\n", s.ID, err)
			break
		}

		// Construct frame without length prefix: [CmdID(2)][Payload]
		outFrame := make([]byte, 2+len(resp.Payload))
		binary.BigEndian.PutUint16(outFrame[0:2], uint16(resp.CmdId))
		copy(outFrame[2:], resp.Payload)

		// Send via KCP (channel 1), falls back to WS if KCP isn't ready
		err = s.Send(1, outFrame)
		if err != nil {
			log.Printf("[Session %d] Failed to send game packet to client via KCP: %v\n", s.ID, err)
		}

		if resp.KickClient {
			s.Close()
		}
	}
}

func (s *Session) Close() {
	s.mu.Lock()
	if !s.Connected {
		s.mu.Unlock()
		return
	}
	s.Connected = false
	s.mu.Unlock()

	if s.GameCancel != nil {
		s.GameCancel()
	}

	if rpc.DefaultRPCManager != nil {
		go rpc.DefaultRPCManager.NotifyDisconnect(&pb.GatewayRequest{
			ConnId: s.ID,
			Uid:    s.UID,
			CmdId:  0,
		})
	}

	if s.WSConn != nil {
		s.WSConn.Close()
	}
	if s.KCPConn != nil {
		s.KCPConn.Close()
	}
	if DefaultManager != nil {
		DefaultManager.RemoveSession(s.ID)
	}
}
