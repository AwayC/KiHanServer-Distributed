package network

import (
	"errors"
	"net"
	"time"
)

type KCPPacket struct {
	Data []byte
	Addr net.Addr
}

type GlobalKCPConn struct {
	readCh chan KCPPacket
	server *UDPServer
}

func NewGlobalKCPConn(server *UDPServer) *GlobalKCPConn {
	return &GlobalKCPConn{
		readCh: make(chan KCPPacket, 1024),
		server: server,
	}
}

func (g *GlobalKCPConn) InjectKCP(data []byte, addr net.Addr) {
	select {
	case g.readCh <- KCPPacket{Data: data, Addr: addr}:
	default:
		// drop packet if channel is full
	}
}

func (g *GlobalKCPConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
	pkt, ok := <-g.readCh
	if !ok {
		return 0, nil, errors.New("GlobalKCPConn closed")
	}
	n = copy(p, pkt.Data)
	return n, pkt.Addr, nil
}

func (g *GlobalKCPConn) WriteTo(p []byte, addr net.Addr) (n int, err error) {
	// Must prepend ID and Key and PacketTypeKCP
	if DefaultManager == nil {
		return 0, errors.New("manager not initialized")
	}
	sess := DefaultManager.GetSessionByAddr(addr.String())
	if sess == nil {
		return 0, errors.New("session not found")
	}

	hdr := EncodeHeader(sess.ID, sess.Key, PacketTypeKCP)
	full := make([]byte, len(hdr)+len(p))
	copy(full, hdr)
	copy(full[len(hdr):], p)

	_, err = g.server.ConnObj.WriteToUDP(full, addr.(*net.UDPAddr))
	return len(p), err
}

func (g *GlobalKCPConn) Close() error {
	close(g.readCh)
	return nil
}

func (g *GlobalKCPConn) LocalAddr() net.Addr {
	return g.server.ConnObj.LocalAddr()
}

func (g *GlobalKCPConn) SetDeadline(t time.Time) error {
	return nil
}

func (g *GlobalKCPConn) SetReadDeadline(t time.Time) error {
	return nil
}

func (g *GlobalKCPConn) SetWriteDeadline(t time.Time) error {
	return nil
}
