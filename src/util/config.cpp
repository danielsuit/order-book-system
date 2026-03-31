#include "config.h"
#include <cstdlib>
#include <sstream>

static std::string get_env(const char* name, const std::string& default_val = "") {
    const char* val = std::getenv(name);
    return val ? std::string(val) : default_val;
}

static int get_env_int(const char* name, int default_val) {
    const char* val = std::getenv(name);
    return val ? std::stoi(val) : default_val;
}

Config Config::from_env() {
    Config cfg;
    cfg.node_id = get_env("NODE_ID", "node-standalone");
    cfg.bind_host = get_env("BIND_HOST", "0.0.0.0");
    cfg.udp_port = static_cast<uint16_t>(get_env_int("UDP_PORT", 7000));
    cfg.tcp_port = static_cast<uint16_t>(get_env_int("TCP_PORT", 8000));
    cfg.gossip_interval_ms = get_env_int("GOSSIP_INTERVAL_MS", 100);
    cfg.heartbeat_interval_ms = get_env_int("HEARTBEAT_INTERVAL_MS", 500);
    cfg.peer_timeout_ms = get_env_int("PEER_TIMEOUT_MS", 2000);
    cfg.gossip_strategy = get_env("GOSSIP_STRATEGY", "RANDOM");

    std::string peers_str = get_env("SEED_PEERS");
    if (!peers_str.empty()) {
        std::istringstream ss(peers_str);
        std::string peer;
        while (std::getline(ss, peer, ',')) {
            auto colon = peer.rfind(':');
            if (colon != std::string::npos) {
                PeerAddr pa;
                pa.host = peer.substr(0, colon);
                pa.port = static_cast<uint16_t>(std::stoi(peer.substr(colon + 1)));
                cfg.seed_peers.push_back(pa);
            }
        }
    }

    return cfg;
}
