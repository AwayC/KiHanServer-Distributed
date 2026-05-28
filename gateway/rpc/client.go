package rpc

import (
	"context"
	"fmt"
	"gateway/pb"
	"log"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var DefaultRPCManager *RPCManager

type RPCManager struct {
	lobbyClient pb.LobbyServiceClient
	gameClient  pb.GameServiceClient
}

func InitRPCManager(lobbyAddr, gameAddr string) error {
	DefaultRPCManager = &RPCManager{}

	// Initialize Lobby gRPC Client
	lobbyConn, err := grpc.Dial(lobbyAddr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return fmt.Errorf("failed to connect to lobby server: %v", err)
	}
	DefaultRPCManager.lobbyClient = pb.NewLobbyServiceClient(lobbyConn)
	log.Printf("Connected to LobbyService at %s", lobbyAddr)

	// Initialize Game gRPC Client
	gameConn, err := grpc.Dial(gameAddr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return fmt.Errorf("failed to connect to game server: %v", err)
	}
	DefaultRPCManager.gameClient = pb.NewGameServiceClient(gameConn)
	log.Printf("Connected to GameService at %s", gameAddr)

	return nil
}

func (m *RPCManager) RouteToLobby(req *pb.GatewayRequest) (*pb.GatewayResponse, error) {
	return m.lobbyClient.HandleRequest(context.Background(), req)
}

func (m *RPCManager) NotifyDisconnect(req *pb.GatewayRequest) error {
	_, err := m.lobbyClient.ClientDisconnect(context.Background(), req)
	return err
}

func (m *RPCManager) GetGameClient() pb.GameServiceClient {
	return m.gameClient
}
