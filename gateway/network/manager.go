package network

import (
	"math/rand"
	"sync"

	"github.com/gorilla/websocket"
)

var DefaultManager *SessionManager

type SessionManager struct {
	mu        sync.RWMutex
	sessions  map[uint32]*Session
	addrMap   map[string]uint32 // UDPAddr.String() -> ID
	UDPServer *UDPServer
}

func NewSessionManager() *SessionManager {
	return &SessionManager{
		sessions: make(map[uint32]*Session),
		addrMap:  make(map[string]uint32),
	}
}

func (m *SessionManager) AddSession(token string, uid string, wsConn *websocket.Conn) *Session {
	m.mu.Lock()
	defer m.mu.Unlock()

	var id uint32
	for {
		id = rand.Uint32()
		if id != 0 && m.sessions[id] == nil {
			break
		}
	}
	
	key := rand.Uint32()
	
	s := NewSession(id, key, token, uid, wsConn)
	m.sessions[id] = s
	return s
}

func (m *SessionManager) GetSession(id uint32) *Session {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.sessions[id]
}

func (m *SessionManager) GetSessionByAddr(addr string) *Session {
	m.mu.RLock()
	defer m.mu.RUnlock()
	if id, ok := m.addrMap[addr]; ok {
		return m.sessions[id]
	}
	return nil
}

func (m *SessionManager) MapUDPAddr(id uint32, addr string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.addrMap[addr] = id
}

func (m *SessionManager) RemoveSession(id uint32) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	s := m.sessions[id]
	if s != nil {
		if s.UDPAddr != nil {
			delete(m.addrMap, s.UDPAddr.String())
		}
		delete(m.sessions, id)
	}
}

func (m *SessionManager) GetSessionsByUID(uid string) []*Session {
	m.mu.RLock()
	defer m.mu.RUnlock()
	
	var list []*Session
	for _, s := range m.sessions {
		if s.UID == uid {
			list = append(list, s)
		}
	}
	return list
}
