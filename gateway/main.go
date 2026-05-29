package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"gateway/config"
	"gateway/network"
	"gateway/rpc"

	"github.com/redis/go-redis/v9"
)

func main() {
	// 1. Load config
	config.LoadConfig("../config.json")
	cfg := config.GlobalConfig
	// 1.5 Init RPC Manager
	err := rpc.InitRPCManager(cfg.Lobby.Addr, cfg.Game.Addr)
	if err != nil {
		fmt.Printf("Warning: Failed to init RPC: %v\n", err)
	}

	// 2. Init Redis
	rdb := redis.NewClient(&redis.Options{
		Addr:     fmt.Sprintf("%s:%d", cfg.Redis.Host, cfg.Redis.Port),
		Password: cfg.Redis.Password,
		DB:       0,
	})

	// 3. Init Session Manager
	network.DefaultManager = network.NewSessionManager()

	// 4. Start UDP Server (for KCP and Raw UDP)
	udpAddr := fmt.Sprintf("%s:%d", cfg.Gateway.Ip, cfg.Gateway.Port)
	udpServer, err := network.NewUDPServer(udpAddr)
	if err != nil {
		panic("Failed to start UDP server: " + err.Error())
	}
	network.DefaultManager.UDPServer = udpServer
	udpServer.Start()
	fmt.Println("UDP/KCP Server listening on", udpAddr)

	// 5. Start TCP Server
	tcpAddr := fmt.Sprintf("%s:%d", cfg.Gateway.Ip, cfg.Gateway.Port)
	tcpServer := network.NewTCPServer(tcpAddr, rdb, cfg.Gateway.Port)
	go func() {
		if err := tcpServer.Start(); err != nil {
			panic("Failed to start TCP server: " + err.Error())
		}
	}()

	// 5.5 Start Redis Pub/Sub for Kicking users
	go func() {
		pubsub := rdb.Subscribe(context.Background(), "gateway:kick")
		defer pubsub.Close()
		ch := pubsub.Channel()
		for msg := range ch {
			var data struct {
				UID      string `json:"uid"`
				NewToken string `json:"new_token"`
			}
			if err := json.Unmarshal([]byte(msg.Payload), &data); err != nil {
				continue
			}

			// Find old sessions and kick them
			sessions := network.DefaultManager.GetSessionsByUID(data.UID)
			for _, s := range sessions {
				if s.Token != data.NewToken {
					fmt.Printf("Kicking old session for UID %s\n", data.UID)
					
					// Optionally send a kick packet here before closing
					// e.g. s.Send(0, kickData)
					
					s.Close()
				}
			}
		}
	}()

	// 6. Wait for interrupt
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh
	fmt.Println("Shutting down gateway...")
}
