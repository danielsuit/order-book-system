#include "orderbook.h"
#include <algorithm>
#include <stdexcept>

std::string OrderBook::add_order(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);

    orders_[order.order_id] = order;
    Order* stored = &orders_[order.order_id];

    if (order.side == Side::BUY) {
        auto& level = bids_[order.price];
        level.price = order.price;
        level.total_quantity += order.remaining;
        level.orders.push_back(stored);
    } else {
        auto& level = asks_[order.price];
        level.price = order.price;
        level.total_quantity += order.remaining;
        level.orders.push_back(stored);
    }

    return order.order_id;
}

bool OrderBook::cancel_order(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = orders_.find(order_id);
    if (it == orders_.end() || it->second.is_cancelled) {
        return false;
    }

    Order& order = it->second;
    order.is_cancelled = true;

    remove_order_from_level(order_id, order.side, order.price);

    return true;
}

std::vector<Fill> OrderBook::match_market_order(Side aggressor_side, int64_t quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Fill> fills;
    int64_t remaining = quantity;

    if (aggressor_side == Side::BUY) {
        // Walk asks from lowest to highest
        auto it = asks_.begin();
        while (it != asks_.end() && remaining > 0) {
            PriceLevel& level = it->second;

            while (!level.orders.empty() && remaining > 0) {
                Order* order = level.orders.front();

                // Skip cancelled orders that haven't been cleaned up
                if (order->is_cancelled) {
                    level.orders.pop_front();
                    continue;
                }

                int64_t fill_qty = std::min(remaining, order->remaining);
                fills.push_back({order->order_id, order->price, fill_qty});

                order->remaining -= fill_qty;
                level.total_quantity -= fill_qty;
                remaining -= fill_qty;

                if (order->remaining == 0) {
                    order->is_cancelled = true;
                    level.orders.pop_front();
                }
            }

            if (level.orders.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        // Walk bids from highest to lowest
        auto it = bids_.begin();
        while (it != bids_.end() && remaining > 0) {
            PriceLevel& level = it->second;

            while (!level.orders.empty() && remaining > 0) {
                Order* order = level.orders.front();

                if (order->is_cancelled) {
                    level.orders.pop_front();
                    continue;
                }

                int64_t fill_qty = std::min(remaining, order->remaining);
                fills.push_back({order->order_id, order->price, fill_qty});

                order->remaining -= fill_qty;
                level.total_quantity -= fill_qty;
                remaining -= fill_qty;

                if (order->remaining == 0) {
                    order->is_cancelled = true;
                    level.orders.pop_front();
                }
            }

            if (level.orders.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
    }

    return fills;
}

std::optional<PriceLevel> OrderBook::get_best_bid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->second;
}

std::optional<PriceLevel> OrderBook::get_best_ask() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->second;
}

BookSnapshot OrderBook::get_snapshot(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    BookSnapshot snap;

    int count = 0;
    for (auto& [price, level] : bids_) {
        if (count >= depth) break;
        snap.bids.push_back(level);
        ++count;
    }

    count = 0;
    for (auto& [price, level] : asks_) {
        if (count >= depth) break;
        snap.asks.push_back(level);
        ++count;
    }

    return snap;
}

std::vector<Order> OrderBook::get_all_orders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> result;
    for (auto& [id, order] : orders_) {
        if (!order.is_cancelled && order.remaining > 0) {
            result.push_back(order);
        }
    }
    return result;
}

bool OrderBook::has_order(const std::string& order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.count(order_id) > 0;
}

void OrderBook::insert_replicated_order(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (orders_.count(order.order_id)) return;  // Already have it

    orders_[order.order_id] = order;
    Order* stored = &orders_[order.order_id];

    if (order.side == Side::BUY) {
        auto& level = bids_[order.price];
        level.price = order.price;
        level.total_quantity += order.remaining;
        level.orders.push_back(stored);
    } else {
        auto& level = asks_[order.price];
        level.price = order.price;
        level.total_quantity += order.remaining;
        level.orders.push_back(stored);
    }
}

void OrderBook::apply_replicated_cancel(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = orders_.find(order_id);
    if (it == orders_.end()) return;  // Don't have this order yet — will be handled by add-wins semantics
    if (it->second.is_cancelled) return;  // Already cancelled

    Order& order = it->second;
    order.is_cancelled = true;
    remove_order_from_level(order_id, order.side, order.price);
}

// NOTE: Linear scan through the deque is O(n) per level. A production system would
// use an unordered_map<string, deque::iterator> for O(1) removal, at the cost of
// iterator invalidation bookkeeping. Acceptable here since price levels are small.
void OrderBook::remove_order_from_level(const std::string& order_id, Side side, double price) {
    if (side == Side::BUY) {
        auto lit = bids_.find(price);
        if (lit != bids_.end()) {
            auto& deq = lit->second.orders;
            auto oit = std::find_if(deq.begin(), deq.end(),
                [&](const Order* o) { return o->order_id == order_id; });
            if (oit != deq.end()) {
                lit->second.total_quantity -= (*oit)->remaining;
                deq.erase(oit);
            }
            if (deq.empty()) bids_.erase(lit);
        }
    } else {
        auto lit = asks_.find(price);
        if (lit != asks_.end()) {
            auto& deq = lit->second.orders;
            auto oit = std::find_if(deq.begin(), deq.end(),
                [&](const Order* o) { return o->order_id == order_id; });
            if (oit != deq.end()) {
                lit->second.total_quantity -= (*oit)->remaining;
                deq.erase(oit);
            }
            if (deq.empty()) asks_.erase(lit);
        }
    }
}
