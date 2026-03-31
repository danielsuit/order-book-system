#pragma once
#include "../orderbook/orderbook.h"
#include "vector_clock.h"
#include <set>
#include <string>
#include <mutex>
#include <vector>

enum class OpType : uint8_t { ADD_ORDER = 0, CANCEL_ORDER = 1 };

struct Operation {
    OpType type;
    Order order;                    // For ADD_ORDER
    std::string cancel_order_id;   // For CANCEL_ORDER
    std::string origin_node;
    VectorClock clock;
    std::string op_id;             // Unique ID for deduplication
};

class CRDTOrderBook {
public:
    explicit CRDTOrderBook(const std::string& node_id);

    // Local operations (from clients connected to THIS node)
    std::string local_add_order(Side side, double price, int64_t quantity);
    bool local_cancel_order(const std::string& order_id);
    std::vector<Fill> local_market_order(Side side, int64_t quantity);

    // Remote operations (from gossip)
    bool apply_remote_operation(const Operation& op);

    // Anti-entropy
    std::vector<Operation> get_operation_log() const;
    std::vector<Operation> get_operations_since(const VectorClock& peer_clock) const;

    VectorClock get_clock() const;
    std::string get_node_id() const { return node_id_; }

    // Delegate to underlying order book
    BookSnapshot get_snapshot(int depth = 10) const;
    std::optional<PriceLevel> get_best_bid() const;
    std::optional<PriceLevel> get_best_ask() const;

    // Metrics
    size_t get_op_log_size() const;
    size_t get_order_count() const;

private:
    std::string node_id_;
    OrderBook book_;
    VectorClock clock_;

    // NOTE: op_log_ grows unbounded — no compaction or GC. Fine for short-lived
    // demos, but a production system would need log truncation once all peers have
    // acknowledged operations (e.g. via a stable vector clock watermark).
    std::vector<Operation> op_log_;
    std::set<std::string> seen_ops_;

    mutable std::mutex mutex_;
};
