package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

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

	// 5. Start WebSocket Server
	wsServer := network.NewWSServer(rdb, cfg.Gateway.Port)
	http.Handle("/ws", wsServer)
	
	wsAddr := fmt.Sprintf("%s:%d", cfg.Gateway.Ip, cfg.Gateway.Port)
	go func() {
		if err := http.ListenAndServe(wsAddr, nil); err != nil {
			panic("Failed to start WebSocket server: " + err.Error())
		}
	}()
	fmt.Println("WebSocket Server listening on", wsAddr)

	// 5.5 Start Redis Pub/Sub for Kicking users
	go func() {
		pubsub := rdb.Subscribe(context.Background(), "gateway:kick")
		defer pubsub.Close()
		ch := pubsub.Channel()
		for msg := range ch {
			var data struct {
				UID      uint32 `json:"uid"`
				NewToken string `json:"new_token"`
			}
			if err := json.Unmarshal([]byte(msg.Payload), &data); err != nil {
				continue
			}

			// Find old sessions and kick them
			sessions := network.DefaultManager.GetSessionsByUID(data.UID)
			for _, s := range sessions {
				if s.Token != data.NewToken {
					fmt.Printf("Kicking old session for UID %d\n", data.UID)
					
					// Optionally send a kick packet here before closing
					// e.g. s.Send(0, kickData)
					
					s.Close()
				}
			}
		}
	}()

	// 5.6 Start Lobby Push Subscription
	go func() {
		for {
			stream, err := rpc.DefaultRPCManager.SubscribeLobby(context.Background())
			if err != nil {
				log.Printf("[LobbyPush] Failed to subscribe: %v. Retrying in 3s...\n", err)
				time.Sleep(3 * time.Second)
				continue
			}
			fmt.Println("[LobbyPush] Successfully subscribed to Lobby push stream")

			for {
				resp, err := stream.Recv()
				if err != nil {
					log.Printf("[LobbyPush] Stream closed: %v. Reconnecting...\n", err)
					break
				}

				// Find session
				sess := network.DefaultManager.GetSession(resp.ConnId)
				if sess != nil {
					log.Printf("[LobbyPush] Pushing CmdID=%d to Session=%d\n", resp.CmdId, resp.ConnId)
					
					// Construct frame: [CmdID(2)][Payload]
					outFrame := make([]byte, 2+len(resp.Payload))
					binary.BigEndian.PutUint16(outFrame[0:2], uint16(resp.CmdId))
					copy(outFrame[2:], resp.Payload)

					// Send via Reliable channel (0) for lobby notifications
					sess.Send(0, outFrame)

					if resp.KickClient {
						sess.Close()
					}
				}
			}
			time.Sleep(1 * time.Second)
		}
	}()

	// 6. Wait for interrupt
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh
	fmt.Println("Shutting down gateway...")
}
