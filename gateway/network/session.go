package network

import (
	"fmt"
	"log"
	"net"
	"sync"
	"time"

	"gateway/pb"
	"gateway/rpc"
	"github.com/xtaci/kcp-go/v5"
)

type Session struct {
	ID    uint32
	Key   uint32
	Token string
	UID   string

	TCPConn net.Conn

	mu            sync.RWMutex
	UDPAddr       *net.UDPAddr
	KCPConn       *kcp.UDPSession
	Connected     bool
	LastHeartbeat time.Time
}

func NewSession(id uint32, key uint32, token string, uid string, tcpConn net.Conn) *Session {
	return &Session{
		ID:            id,
		Key:           key,
		Token:         token,
		UID:           uid,
		TCPConn:       tcpConn,
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

// Channel 0: TCP, Channel 1: KCP, Channel 2: Raw UDP
func (s *Session) Send(channel int, data []byte) error {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if !s.Connected {
		return net.ErrClosed
	}

	switch channel {
	case 1:
		if s.KCPConn != nil {
			_, err := s.KCPConn.Write(data)
			return err
		}
		// Fallback to TCP if KCP not ready
		fallthrough
	case 2:
		if s.UDPAddr != nil && DefaultManager != nil && DefaultManager.UDPServer != nil {
			hdr := EncodeHeader(s.ID, s.Key, PacketTypeRawUDP)
			full := append(hdr, data...)
			_, err := DefaultManager.UDPServer.ConnObj.WriteToUDP(full, s.UDPAddr)
			return err
		}
		// Fallback to TCP
		fallthrough
	case 0:
		fallthrough
	default:
		// TODO: Add length prefix framing for TCP
		_, err := s.TCPConn.Write(data)
		return err
	}
}

func (s *Session) DispatchPacket(cmdID uint16, payload []byte) {
	if rpc.DefaultRPCManager == nil {
		log.Println("RPC Manager not initialized")
		return
	}

	req := &pb.GatewayRequest{
		ConnId:  s.ID,
		Uid:     s.UID,
		CmdId:   uint32(cmdID),
		Payload: payload,
	}

	// Simple routing logic: CmdID < 2000 -> Lobby, else -> Game
	if cmdID < 2000 {
		resp, err := rpc.DefaultRPCManager.RouteToLobby(req)
		if err != nil {
			log.Printf("Lobby RPC error for ID %d: %v\n", s.ID, err)
			return
		}
		
		if resp != nil {
			// Send response back to client via best channel
			// If it's a critical lobby message, we can force channel 0 (TCP), 
			// but for now let's just use channel 1 (KCP preferred, fallback to TCP)
			outFrame := EncodeAppFrame(uint16(resp.CmdId), resp.Payload)
			s.Send(1, outFrame)

			if resp.KickClient {
				s.Close()
			}
		}
	} else {
		// Route to Game Server
		// For stream we might need a persistent stream per session,
		// but for now we do a simple unary-like push if using stream.
		// A full implementation would manage a stream per session.
		// This is a placeholder for the stream integration.
		fmt.Printf("TODO: Route CmdID %d to GameServer via Stream\n", cmdID)
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

	if rpc.DefaultRPCManager != nil {
		go rpc.DefaultRPCManager.NotifyDisconnect(&pb.GatewayRequest{
			ConnId: s.ID,
			Uid:    s.UID,
			CmdId:  0, // 0 can signify disconnect or unused
		})
	}

	if s.TCPConn != nil {
		s.TCPConn.Close()
	}
	if s.KCPConn != nil {
		s.KCPConn.Close()
	}
	if DefaultManager != nil {
		DefaultManager.RemoveSession(s.ID)
	}
}
