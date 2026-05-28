package config

import (
	"encoding/json"
	"os"
)

type Config struct {
	Redis struct {
		Host     string `json:"host"`
		Port     int    `json:"port"`
		Password string `json:"password"`
	} `json:"redis"`
	Gateway struct {
		Ip   string `json:"ip"`
		Port int    `json:"port"`
	} `json:"gateway"`
	Lobby struct {
		Addr string `json:"addr"`
	} `json:"lobby"`
	Game struct {
		Addr string `json:"addr"`
	} `json:"game"`
}

var GlobalConfig *Config

func LoadConfig(path string) {
	data, err := os.ReadFile(path)
	if err != nil {
		panic("Failed to read config: " + err.Error())
	}
	GlobalConfig = &Config{}
	if err := json.Unmarshal(data, GlobalConfig); err != nil {
		panic("Failed to parse config: " + err.Error())
	}
}
