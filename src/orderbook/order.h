#pragma once
#include <string>
#include <cstdint>
#include <tuple>

enum class Side : uint8_t { BUY = 0, SELL = 1 };

struct Order {
    std::string order_id;
    std::string node_id;
    Side side;
    double price;
    int64_t quantity;
    int64_t remaining;
    uint64_t timestamp;  // Lamport timestamp from vector clock
    bool is_cancelled = false;

    // Total ordering for conflict resolution
    // Primary: timestamp (higher = newer)
    // Tiebreaker: node_id + order_id lexicographic
    bool operator<(const Order& other) const {
        if (timestamp != other.timestamp) return timestamp < other.timestamp;
        if (node_id != other.node_id) return node_id < other.node_id;
        return order_id < other.order_id;
    }

    bool operator==(const Order& other) const {
        return order_id == other.order_id;
    }
};

struct Fill {
    std::string order_id;
    double price;
    int64_t quantity;
};
