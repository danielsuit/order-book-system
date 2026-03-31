#pragma once
#include <map>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>
#include "order.h"
#include "price_level.h"

class OrderBook {
public:
    // Add a new order to the book. Returns the order_id.
    std::string add_order(const Order& order);

    // Cancel an order. Returns false if order not found.
    bool cancel_order(const std::string& order_id);

    // Execute a market order against the book. Returns fills.
    std::vector<Fill> match_market_order(Side aggressor_side, int64_t quantity);

    // Query
    std::optional<PriceLevel> get_best_bid() const;
    std::optional<PriceLevel> get_best_ask() const;
    BookSnapshot get_snapshot(int depth = 10) const;

    // For CRDT: get all active orders
    std::vector<Order> get_all_orders() const;

    // For CRDT: check if an order exists
    bool has_order(const std::string& order_id) const;

    // For CRDT: directly insert an order from replication (skips matching)
    void insert_replicated_order(const Order& order);

    // For CRDT: mark an order cancelled from replication
    void apply_replicated_cancel(const std::string& order_id);

private:
    void remove_order_from_level(const std::string& order_id, Side side, double price);

    // Bids: highest price first
    std::map<double, PriceLevel, std::greater<double>> bids_;
    // Asks: lowest price first
    std::map<double, PriceLevel> asks_;
    // Fast lookup by order_id
    std::unordered_map<std::string, Order> orders_;

    mutable std::mutex mutex_;
};
