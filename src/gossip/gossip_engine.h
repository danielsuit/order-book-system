#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include "../network/udp_transport.h"
#include "../crdt/crdt_orderbook.h"
#include "peer.h"
#include "message.h"
#include "wythoff_scheduler.h"

enum class GossipStrategy {
    RANDOM,
    ROUND_ROBIN,
    WYTHOFF
};

class GossipEngine {
public:
    GossipEngine(
        const std::string& node_id,
        CRDTOrderBook& crdt_book,
        UDPTransport& transport,
        const std::vector<PeerInfo>& seed_peers,
        int gossip_interval_ms = 100,
        int heartbeat_interval_ms = 500,
        int peer_timeout_ms = 2000,
        GossipStrategy strategy = GossipStrategy::RANDOM
    );

    void start();
    void stop();

    std::vector<PeerInfo> get_peers() const;

private:
    void gossip_loop();
    void receive_loop();
    void do_gossip_round();
    void handle_message(const Message& msg, const std::string& sender_host, uint16_t sender_port);
    PeerInfo* select_random_peer();
    PeerInfo* select_round_robin_peer();
    PeerInfo* select_wythoff_peer(SyncMode& mode);
    void request_full_sync(const PeerInfo& peer);
    void send_message(const Message& msg, const std::string& host, uint16_t port);

    std::string node_id_;
    CRDTOrderBook& crdt_book_;
    UDPTransport& transport_;

    std::vector<PeerInfo> peers_;
    mutable std::mutex peers_mutex_;

    std::atomic<bool> running_{false};
    std::thread gossip_thread_;
    std::thread receive_thread_;

    int gossip_interval_ms_;
    int heartbeat_interval_ms_;
    int peer_timeout_ms_;
    GossipStrategy strategy_;

    // Wythoff state
    WythoffScheduler scheduler_;
    uint64_t round_{0};
    uint32_t my_node_index_{0};

    // Round-robin state
    size_t rr_index_{0};
};
