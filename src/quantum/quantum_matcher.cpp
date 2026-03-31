#include "quantum_matcher.h"
#include "../util/logger.h"
#include <algorithm>

std::vector<QuantumFill> QuantumMatcher::optimize_batch(const OrderBook& book) {
    auto snapshot = book.get_snapshot(1000);  // Get deep snapshot
    auto all_orders = book.get_all_orders();

    // Separate into bids and asks
    std::vector<Order> bids, asks;
    for (auto& order : all_orders) {
        if (!order.is_cancelled && order.remaining > 0) {
            if (order.side == Side::BUY) bids.push_back(order);
            else asks.push_back(order);
        }
    }

    return optimize(bids, asks);
}

std::vector<QuantumFill> QuantumMatcher::optimize(
        const std::vector<Order>& bids,
        const std::vector<Order>& asks) {

    std::vector<QuantumFill> fills;

    if (bids.empty() || asks.empty()) return fills;

    auto& bridge = JuliaBridge::instance();
    if (!bridge.is_initialized()) {
        LOG_ERROR("QuantumMatcher: Julia bridge not initialized");
        return fills;
    }

    // Prepare vectors for Julia
    std::vector<double> bid_prices;
    std::vector<int64_t> bid_quantities;
    std::vector<double> ask_prices;
    std::vector<int64_t> ask_quantities;

    bid_prices.reserve(bids.size());
    bid_quantities.reserve(bids.size());
    for (auto& b : bids) {
        bid_prices.push_back(b.price);
        bid_quantities.push_back(b.remaining);
    }

    ask_prices.reserve(asks.size());
    ask_quantities.reserve(asks.size());
    for (auto& a : asks) {
        ask_prices.push_back(a.price);
        ask_quantities.push_back(a.remaining);
    }

    // Call QAOA optimizer via Julia bridge
    auto matches = bridge.optimize_matching(
        bid_prices, bid_quantities, ask_prices, ask_quantities);

    // Convert QuantumMatch to QuantumFill with order IDs
    fills.reserve(matches.size());
    for (auto& m : matches) {
        if (m.bid_idx >= 0 && m.bid_idx < static_cast<int>(bids.size()) &&
            m.ask_idx >= 0 && m.ask_idx < static_cast<int>(asks.size())) {
            QuantumFill fill;
            fill.bid_order_id = bids[m.bid_idx].order_id;
            fill.ask_order_id = asks[m.ask_idx].order_id;
            fill.exec_price = m.exec_price;
            fill.quantity = m.quantity;
            fill.surplus = bids[m.bid_idx].price - asks[m.ask_idx].price;
            fills.push_back(fill);
        }
    }

    LOG_INFO("QuantumMatcher: QAOA found " + std::to_string(fills.size()) +
             " optimal matches from " + std::to_string(bids.size()) + " bids x " +
             std::to_string(asks.size()) + " asks");

    return fills;
}

QuantumMatcher::ComparisonResult QuantumMatcher::compare(const OrderBook& book) {
    ComparisonResult result;
    auto all_orders = book.get_all_orders();

    std::vector<Order> bids, asks;
    for (auto& order : all_orders) {
        if (!order.is_cancelled && order.remaining > 0) {
            if (order.side == Side::BUY) bids.push_back(order);
            else asks.push_back(order);
        }
    }

    // Quantum matching
    result.quantum_fills = optimize(bids, asks);
    result.quantum_surplus = 0.0;
    for (auto& f : result.quantum_fills) {
        result.quantum_surplus += f.surplus * f.quantity;
    }

    // Classical greedy matching (sorted by surplus descending)
    struct Candidate {
        int bid_idx;
        int ask_idx;
        double surplus;
        int64_t quantity;
        double exec_price;
    };

    std::vector<Candidate> candidates;
    for (size_t i = 0; i < bids.size(); ++i) {
        for (size_t j = 0; j < asks.size(); ++j) {
            double surplus = bids[i].price - asks[j].price;
            if (surplus >= 0) {
                int64_t qty = std::min(bids[i].remaining, asks[j].remaining);
                double price = (bids[i].price + asks[j].price) / 2.0;
                candidates.push_back({
                    static_cast<int>(i), static_cast<int>(j),
                    surplus, qty, price});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.surplus * a.quantity > b.surplus * b.quantity;
        });

    std::vector<bool> used_bids(bids.size(), false);
    std::vector<bool> used_asks(asks.size(), false);

    result.classical_surplus = 0.0;
    for (auto& c : candidates) {
        if (!used_bids[c.bid_idx] && !used_asks[c.ask_idx]) {
            QuantumFill fill;
            fill.bid_order_id = bids[c.bid_idx].order_id;
            fill.ask_order_id = asks[c.ask_idx].order_id;
            fill.exec_price = c.exec_price;
            fill.quantity = c.quantity;
            fill.surplus = c.surplus;
            result.classical_fills.push_back(fill);
            result.classical_surplus += c.surplus * c.quantity;
            used_bids[c.bid_idx] = true;
            used_asks[c.ask_idx] = true;
        }
    }

    LOG_INFO("QuantumMatcher comparison: quantum_surplus=" +
             std::to_string(result.quantum_surplus) +
             " classical_surplus=" + std::to_string(result.classical_surplus));

    return result;
}
