#include "util/config.h"
#include "util/logger.h"
#include "crdt/crdt_orderbook.h"
#include "network/udp_transport.h"
#include "network/tcp_server.h"
#include "gossip/gossip_engine.h"
#include "api/client_handler.h"
#include "quantum/julia_bridge.h"
#include "quantum/quantum_matcher.h"

#include <csignal>
#include <atomic>
#include <iostream>
#include <filesystem>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

int main() {
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto config = Config::from_env();

    Logger::instance().set_node_id(config.node_id);
    LOG_INFO("Starting node " + config.node_id);

    // Initialize Julia quantum bridge if quantum strategy is requested
    // or if QUANTUM_ENABLED=1 is set
    std::string quantum_env = std::getenv("QUANTUM_ENABLED") ? std::getenv("QUANTUM_ENABLED") : "0";
    bool quantum_requested = (config.gossip_strategy == "QUANTUM" ||
                              config.gossip_strategy == "QUANTUM_HYBRID" ||
                              quantum_env == "1");

    if (quantum_requested) {
        // Look for Julia scripts relative to the executable, or via env var
        std::string julia_dir;
        if (std::getenv("JULIA_QUANTUM_DIR")) {
            julia_dir = std::getenv("JULIA_QUANTUM_DIR");
        } else {
            // Default: look in /app/quantum/julia (Docker) or ./src/quantum/julia (dev)
            if (std::filesystem::exists("/app/quantum/julia/init.jl")) {
                julia_dir = "/app/quantum/julia";
            } else if (std::filesystem::exists("src/quantum/julia/init.jl")) {
                julia_dir = "src/quantum/julia";
            } else {
                julia_dir = "/usr/local/share/convergence/quantum/julia";
            }
        }

        if (!JuliaBridge::instance().initialize(julia_dir)) {
            LOG_WARN("Julia quantum bridge initialization failed — falling back to classical strategies");
        } else {
            LOG_INFO("Quantum computing bridge ready (Julia + Yao.jl)");
        }
    }

    CRDTOrderBook crdt_book(config.node_id);
    UDPTransport udp(config.bind_host, config.udp_port);

    std::vector<PeerInfo> peers;
    for (auto& pa : config.seed_peers) {
        PeerInfo pi;
        pi.node_id = pa.host;  // Will be updated on first message
        pi.host = pa.host;
        pi.port = pa.port;
        peers.push_back(pi);
    }

    GossipStrategy strategy = GossipStrategy::RANDOM;
    if (config.gossip_strategy == "WYTHOFF") strategy = GossipStrategy::WYTHOFF;
    else if (config.gossip_strategy == "ROUND_ROBIN") strategy = GossipStrategy::ROUND_ROBIN;
    else if (config.gossip_strategy == "QUANTUM") strategy = GossipStrategy::QUANTUM;
    else if (config.gossip_strategy == "QUANTUM_HYBRID") strategy = GossipStrategy::QUANTUM_HYBRID;

    GossipEngine gossip(config.node_id, crdt_book, udp, peers,
                        config.gossip_interval_ms,
                        config.heartbeat_interval_ms,
                        config.peer_timeout_ms,
                        strategy);

    // Client handler with access to gossip for PEERS command
    auto handler = [&](const std::string& request) -> std::string {
        // Handle PEERS command specially since it needs gossip engine
        if (request.substr(0, 5) == "PEERS") {
            auto peer_list = gossip.get_peers();
            std::string out = "PEERS " + std::to_string(peer_list.size());
            for (auto& p : peer_list) {
                out += "\n" + p.node_id + " " + p.host + ":" + std::to_string(p.port)
                     + " " + (p.is_alive ? "ALIVE" : "DEAD");
            }
            return out;
        }
        return ClientHandler::handle(request, crdt_book);
    };

    TCPServer tcp(config.bind_host, config.tcp_port, handler);

    gossip.start();
    tcp.start();

    LOG_INFO("Node " + config.node_id + " ready (TCP:" + std::to_string(config.tcp_port)
             + " UDP:" + std::to_string(config.udp_port) + ")");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("Shutting down " + config.node_id);
    gossip.stop();
    tcp.stop();

    if (JuliaBridge::instance().is_initialized()) {
        JuliaBridge::instance().shutdown();
    }

    return 0;
}
