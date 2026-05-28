package network

import (
	"encoding/binary"
	"errors"
	"io"
)

const (
	PacketTypeHandshake uint8 = 0
	PacketTypeKCP       uint8 = 1
	PacketTypeRawUDP    uint8 = 2
)

const HeaderSize = 9

func EncodeHeader(id uint32, key uint32, pType uint8) []byte {
	buf := make([]byte, HeaderSize)
	binary.BigEndian.PutUint32(buf[0:4], id)
	binary.BigEndian.PutUint32(buf[4:8], key)
	buf[8] = pType
	return buf
}

func DecodeHeader(buf []byte) (id uint32, key uint32, pType uint8, err error) {
	if len(buf) < HeaderSize {
		return 0, 0, 0, errors.New("packet too short")
	}
	id = binary.BigEndian.Uint32(buf[0:4])
	key = binary.BigEndian.Uint32(buf[4:8])
	pType = buf[8]
	return id, key, pType, nil
}

// ---------------------------------------------------------
// Application Framing (Client <-> Gateway)
// Format: [Length uint16][CmdID uint16][Payload bytes...]
// Length = 2 bytes (CmdID) + len(Payload)
// ---------------------------------------------------------

const AppHeaderSize = 4 // 2 bytes Len + 2 bytes CmdID

func EncodeAppFrame(cmdID uint16, payload []byte) []byte {
	totalLen := uint16(2 + len(payload))
	buf := make([]byte, 2+totalLen) // 2 bytes for Length itself
	binary.BigEndian.PutUint16(buf[0:2], totalLen)
	binary.BigEndian.PutUint16(buf[2:4], cmdID)
	copy(buf[4:], payload)
	return buf
}

func DecodeAppFrameHeader(buf []byte) (totalLen uint16, cmdID uint16, err error) {
	if len(buf) < AppHeaderSize {
		return 0, 0, errors.New("frame header too short")
	}
	totalLen = binary.BigEndian.Uint16(buf[0:2])
	cmdID = binary.BigEndian.Uint16(buf[2:4])
	return totalLen, cmdID, nil
}

// ReadAppFrame reads exactly one application frame from an io.Reader
func ReadAppFrame(r io.Reader) (cmdID uint16, payload []byte, err error) {
	headerBuf := make([]byte, AppHeaderSize)
	if _, err := io.ReadFull(r, headerBuf); err != nil {
		return 0, nil, err
	}
	totalLen, cmdID, err := DecodeAppFrameHeader(headerBuf)
	if err != nil {
		return 0, nil, err
	}
	
	payloadLen := int(totalLen) - 2
	if payloadLen < 0 {
		return 0, nil, errors.New("invalid frame length")
	}
	
	if payloadLen == 0 {
		return cmdID, []byte{}, nil
	}

	payloadBuf := make([]byte, payloadLen)
	if _, err := io.ReadFull(r, payloadBuf); err != nil {
		return 0, nil, err
	}
	
	return cmdID, payloadBuf, nil
}
