#pragma once
#include <string>
#include <vector>

struct PeerAddr {
    std::string host;
    uint16_t port;
};

struct Config {
    std::string node_id;
    std::string bind_host = "0.0.0.0";
    uint16_t udp_port = 7000;
    uint16_t tcp_port = 8000;
    std::vector<PeerAddr> seed_peers;
    int gossip_interval_ms = 100;
    int heartbeat_interval_ms = 500;
    int peer_timeout_ms = 2000;
    std::string gossip_strategy = "RANDOM";  // RANDOM, ROUND_ROBIN, WYTHOFF

    static Config from_env();
};
