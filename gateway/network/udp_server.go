package network

import (
	"fmt"
	"net"

	"github.com/xtaci/kcp-go/v5"
)

type UDPServer struct {
	Conn    *net.UDPAddr
	ConnObj *net.UDPConn
	KCPConn *GlobalKCPConn
	KCPLis  *kcp.Listener
}

func NewUDPServer(addr string) (*UDPServer, error) {
	udpAddr, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return nil, err
	}
	conn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		return nil, err
	}

	us := &UDPServer{
		Conn:    udpAddr,
		ConnObj: conn,
	}
	us.KCPConn = NewGlobalKCPConn(us)

	lis, err := kcp.ServeConn(nil, 10, 3, us.KCPConn)
	if err != nil {
		return nil, err
	}
	us.KCPLis = lis

	return us, nil
}

func (u *UDPServer) Start() {
	go u.readLoop()
	go u.acceptKCPLoop()
}

func (u *UDPServer) readLoop() {
	buf := make([]byte, 65535)
	for {
		n, addr, err := u.ConnObj.ReadFromUDP(buf)
		if err != nil {
			fmt.Println("UDP Read Error:", err)
			continue
		}

		if n < HeaderSize {
			continue
		}

		id, key, pType, err := DecodeHeader(buf[:HeaderSize])
		if err != nil {
			continue
		}

		if DefaultManager == nil {
			continue
		}

		sess := DefaultManager.GetSession(id)
		if sess == nil || sess.Key != key {
			continue // Unauthorized or invalid session
		}

		// Update or bind address
		if sess.UDPAddr == nil {
			sess.SetUDPAddr(addr)
			DefaultManager.MapUDPAddr(id, addr.String())
			fmt.Printf("UDP Handshake successful for ID: %d\n", id)
		} else {
			sess.UpdateHeartbeat()
		}

		payload := make([]byte, n-HeaderSize)
		copy(payload, buf[HeaderSize:n])

		switch pType {
		case PacketTypeHandshake:
			// Handshake / Ping
			// Respond with a simple ACK
			ack := EncodeHeader(id, key, PacketTypeHandshake)
			u.ConnObj.WriteToUDP(ack, addr)

		case PacketTypeKCP:
			// Route to KCP Conn
			u.KCPConn.InjectKCP(payload, addr)

		case PacketTypeRawUDP:
			// Route to raw UDP handler (for application to read)
			// TODO: Add a callback or channel in Session to receive raw UDP data
			fmt.Printf("Received raw UDP from ID: %d, length: %d\n", id, len(payload))
		}
	}
}

func (u *UDPServer) acceptKCPLoop() {
	for {
		conn, err := u.KCPLis.AcceptKCP()
		if err != nil {
			fmt.Println("KCP Accept Error:", err)
			continue
		}
		
		addr := conn.RemoteAddr().String()
		if DefaultManager != nil {
			sess := DefaultManager.GetSessionByAddr(addr)
			if sess != nil {
				sess.SetKCPConn(conn)
				fmt.Printf("KCP Connection established for ID: %d\n", sess.ID)
				
				// Start a goroutine to read KCP data
				go func(s *Session, c *kcp.UDPSession) {
					defer s.SetKCPConn(nil)
					for {
						cmdID, payload, err := ReadAppFrame(c)
						if err != nil {
							fmt.Printf("KCP stream closed for ID %d: %v\n", s.ID, err)
							break
						}
						
						s.DispatchPacket(cmdID, payload)
					}
				}(sess, conn)
			} else {
				conn.Close()
			}
		}
	}
}
