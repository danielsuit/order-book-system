#include "gossip_engine.h"
#include "../util/logger.h"
#include <random>
#include <chrono>
#include <algorithm>

static const char* strategy_name(GossipStrategy s) {
    switch (s) {
    case GossipStrategy::RANDOM: return "RANDOM";
    case GossipStrategy::ROUND_ROBIN: return "ROUND_ROBIN";
    case GossipStrategy::WYTHOFF: return "WYTHOFF";
    }
    return "UNKNOWN";
}

GossipEngine::GossipEngine(
    const std::string& node_id,
    CRDTOrderBook& crdt_book,
    UDPTransport& transport,
    const std::vector<PeerInfo>& seed_peers,
    int gossip_interval_ms,
    int heartbeat_interval_ms,
    int peer_timeout_ms,
    GossipStrategy strategy)
    : node_id_(node_id)
    , crdt_book_(crdt_book)
    , transport_(transport)
    , peers_(seed_peers)
    , gossip_interval_ms_(gossip_interval_ms)
    , heartbeat_interval_ms_(heartbeat_interval_ms)
    , peer_timeout_ms_(peer_timeout_ms)
    , strategy_(strategy)
    , scheduler_(static_cast<uint32_t>(seed_peers.size() + 1)) // +1 for self
{
    // Build a sorted list of all node IDs (peers + self) to assign stable indices.
    // This must be deterministic across all nodes — sorting by ID guarantees that.
    std::vector<std::string> all_ids;
    all_ids.push_back(node_id_);
    for (const auto& p : seed_peers) {
        all_ids.push_back(p.node_id);
    }
    std::sort(all_ids.begin(), all_ids.end());
    all_ids.erase(std::unique(all_ids.begin(), all_ids.end()), all_ids.end());

    for (uint32_t i = 0; i < all_ids.size(); ++i) {
        if (all_ids[i] == node_id_) {
            my_node_index_ = i;
            break;
        }
    }
}

void GossipEngine::start() {
    running_ = true;
    gossip_thread_ = std::thread(&GossipEngine::gossip_loop, this);
    receive_thread_ = std::thread(&GossipEngine::receive_loop, this);
    LOG_INFO("Gossip engine started for " + node_id_ + " strategy=" + strategy_name(strategy_));
}

void GossipEngine::stop() {
    running_ = false;
    if (gossip_thread_.joinable()) gossip_thread_.join();
    if (receive_thread_.joinable()) receive_thread_.join();
}

std::vector<PeerInfo> GossipEngine::get_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return peers_;
}

void GossipEngine::gossip_loop() {
    auto last_heartbeat = std::chrono::steady_clock::now();

    while (running_) {
        auto now = std::chrono::steady_clock::now();

        // Gossip round
        do_gossip_round();

        // Heartbeat
        auto hb_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count();
        if (hb_elapsed >= heartbeat_interval_ms_) {
            PeerInfo* peer = select_random_peer();
            if (peer) {
                Message hb;
                hb.type = MessageType::HEARTBEAT;
                hb.sender_id = node_id_;
                hb.sender_clock = crdt_book_.get_clock();
                send_message(hb, peer->host, peer->port);
            }
            last_heartbeat = now;
        }

        // Check peer liveness
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (auto& peer : peers_) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - peer.last_heard).count();
                if (elapsed > peer_timeout_ms_ && peer.is_alive) {
                    peer.is_alive = false;
                    LOG_WARN("Peer " + peer.node_id + " marked dead (timeout)");
                }
            }
        }

        // Sleep — Wythoff uses phase offset for anti-synchronization
        if (strategy_ == GossipStrategy::WYTHOFF) {
            double offset = scheduler_.get_phase_offset(my_node_index_,
                                                        static_cast<double>(gossip_interval_ms_));
            auto sleep_ms = static_cast<int>(gossip_interval_ms_ + offset);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(gossip_interval_ms_));
        }
    }
}

void GossipEngine::do_gossip_round() {
    PeerInfo* peer = nullptr;
    SyncMode mode = SyncMode::PUSH;

    switch (strategy_) {
    case GossipStrategy::RANDOM:
        peer = select_random_peer();
        break;
    case GossipStrategy::ROUND_ROBIN:
        peer = select_round_robin_peer();
        break;
    case GossipStrategy::WYTHOFF:
        peer = select_wythoff_peer(mode);
        break;
    }

    if (!peer) return;

    auto clock = crdt_book_.get_clock();

    bool use_pull = (strategy_ == GossipStrategy::WYTHOFF) &&
                    mode == SyncMode::PULL;

    if (use_pull) {
        // Pull: request state from peer
        Message msg;
        msg.type = MessageType::GOSSIP_PULL;
        msg.sender_id = node_id_;
        msg.sender_clock = clock;
        send_message(msg, peer->host, peer->port);
    } else {
        // Push or push-pull (default for RANDOM/ROUND_ROBIN)
        auto ops = crdt_book_.get_operation_log();
        std::vector<Operation> to_send;
        size_t max_ops = std::min(ops.size(), static_cast<size_t>(20));
        if (ops.size() > max_ops) {
            to_send.assign(ops.end() - static_cast<ptrdiff_t>(max_ops), ops.end());
        } else {
            to_send = ops;
        }

        Message msg;
        msg.type = (strategy_ == GossipStrategy::WYTHOFF)
                       ? MessageType::GOSSIP_PUSH
                       : MessageType::GOSSIP_PUSH_PULL;
        msg.sender_id = node_id_;
        msg.sender_clock = clock;
        msg.operations = to_send;
        send_message(msg, peer->host, peer->port);
    }
}

void GossipEngine::receive_loop() {
    while (running_) {
        std::string sender_host;
        uint16_t sender_port;
        auto data = transport_.recv_from(sender_host, sender_port, 500);
        if (data.empty()) continue;

        try {
            Message msg = Message::deserialize(data.data(), data.size());
            handle_message(msg, sender_host, sender_port);
        } catch (const std::exception& e) {
            LOG_WARN("Failed to deserialize message: " + std::string(e.what()));
        }
    }
}

void GossipEngine::handle_message(const Message& msg, const std::string& sender_host, uint16_t sender_port) {
    // Update peer last_heard
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& peer : peers_) {
            if (peer.node_id == msg.sender_id || peer.host == sender_host) {
                bool was_dead = !peer.is_alive;
                peer.is_alive = true;
                peer.last_heard = std::chrono::steady_clock::now();
                peer.node_id = msg.sender_id;
                if (was_dead) {
                    LOG_INFO("Peer " + peer.node_id + " came back alive, requesting full sync");
                    // Trigger anti-entropy
                    Message req;
                    req.type = MessageType::ANTI_ENTROPY_REQ;
                    req.sender_id = node_id_;
                    req.sender_clock = crdt_book_.get_clock();
                    send_message(req, peer.host, peer.port);
                }
                break;
            }
        }
    }

    switch (msg.type) {
    case MessageType::GOSSIP_PUSH:
    case MessageType::GOSSIP_PUSH_PULL: {
        for (auto& op : msg.operations) {
            crdt_book_.apply_remote_operation(op);
        }

        if (msg.type == MessageType::GOSSIP_PUSH_PULL) {
            // Respond with our ops that the sender might be missing
            auto our_ops = crdt_book_.get_operations_since(msg.sender_clock);
            if (!our_ops.empty()) {
                // Limit response size
                if (our_ops.size() > 20) {
                    our_ops.resize(20);
                }
                Message response;
                response.type = MessageType::GOSSIP_PUSH;
                response.sender_id = node_id_;
                response.sender_clock = crdt_book_.get_clock();
                response.operations = our_ops;
                send_message(response, sender_host, sender_port);
            }
        }
        break;
    }
    case MessageType::GOSSIP_PULL: {
        auto ops = crdt_book_.get_operations_since(msg.sender_clock);
        if (!ops.empty()) {
            Message response;
            response.type = MessageType::GOSSIP_PUSH;
            response.sender_id = node_id_;
            response.sender_clock = crdt_book_.get_clock();
            response.operations = ops;
            send_message(response, sender_host, sender_port);
        }
        break;
    }
    case MessageType::HEARTBEAT:
        // Already updated last_heard above
        break;
    case MessageType::ANTI_ENTROPY_REQ: {
        auto ops = crdt_book_.get_operations_since(msg.sender_clock);
        Message response;
        response.type = MessageType::ANTI_ENTROPY_RES;
        response.sender_id = node_id_;
        response.sender_clock = crdt_book_.get_clock();
        response.operations = ops;
        send_message(response, sender_host, sender_port);
        break;
    }
    case MessageType::ANTI_ENTROPY_RES: {
        for (auto& op : msg.operations) {
            crdt_book_.apply_remote_operation(op);
        }
        break;
    }
    }
}

PeerInfo* GossipEngine::select_random_peer() {
    std::lock_guard<std::mutex> lock(peers_mutex_);

    std::vector<size_t> alive_indices;
    for (size_t i = 0; i < peers_.size(); ++i) {
        if (peers_[i].is_alive) alive_indices.push_back(i);
    }
    if (alive_indices.empty()) {
        // Try any peer when all are marked dead
        if (peers_.empty()) return nullptr;
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, peers_.size() - 1);
        return &peers_[dist(gen)];
    }

    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, alive_indices.size() - 1);
    return &peers_[alive_indices[dist(gen)]];
}

PeerInfo* GossipEngine::select_round_robin_peer() {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    if (peers_.empty()) return nullptr;

    // Cycle through all peers in order
    size_t start = rr_index_;
    do {
        rr_index_ = (rr_index_ + 1) % peers_.size();
        if (peers_[rr_index_].is_alive) {
            return &peers_[rr_index_];
        }
    } while (rr_index_ != start);

    // All dead — return any peer
    return &peers_[rr_index_];
}

PeerInfo* GossipEngine::select_wythoff_peer(SyncMode& mode) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    if (peers_.empty()) return nullptr;

    round_++;
    auto pair = scheduler_.get_pair(round_);
    mode = scheduler_.get_sync_mode(round_);

    // Only act if this node is the sender for this round
    if (pair.sender != my_node_index_) return nullptr;

    // Map receiver index to a peer. The receiver index is among all nodes
    // (peers + self). We need to convert to a peers_ vector index.
    // Build the same sorted ID list to find which peer the receiver maps to.
    std::vector<std::string> all_ids;
    all_ids.push_back(node_id_);
    for (const auto& p : peers_) {
        all_ids.push_back(p.node_id);
    }
    std::sort(all_ids.begin(), all_ids.end());
    all_ids.erase(std::unique(all_ids.begin(), all_ids.end()), all_ids.end());

    if (pair.receiver >= all_ids.size()) return nullptr;
    const std::string& target_id = all_ids[pair.receiver];

    // Find this peer in peers_ vector
    for (auto& peer : peers_) {
        if (peer.node_id == target_id) {
            return &peer;
        }
    }

    return nullptr;
}

void GossipEngine::request_full_sync(const PeerInfo& peer) {
    Message msg;
    msg.type = MessageType::ANTI_ENTROPY_REQ;
    msg.sender_id = node_id_;
    msg.sender_clock = crdt_book_.get_clock();
    send_message(msg, peer.host, peer.port);
}

void GossipEngine::send_message(const Message& msg, const std::string& host, uint16_t port) {
    auto data = msg.serialize();

    // Split if too large (>8000 bytes)
    if (data.size() <= 8000) {
        transport_.send_to(host, port, data.data(), data.size());
    } else {
        // For large messages, just send — fragmentation handled by IP layer for now
        // In production, implement application-level chunking
        transport_.send_to(host, port, data.data(), data.size());
    }
}
