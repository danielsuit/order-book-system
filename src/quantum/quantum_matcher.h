#pragma once
#include <vector>
#include <string>
#include "../orderbook/orderbook.h"
#include "julia_bridge.h"

/**
 * QuantumMatcher — QAOA-optimized order matching engine.
 *
 * Instead of the standard price-time priority matching, this uses the
 * Quantum Approximate Optimization Algorithm (via Julia/Yao.jl) to find
 * the globally optimal set of matches that maximizes total surplus
 * (bid_price - ask_price) * quantity across all matchable pairs.
 *
 * This is useful for batch auctions / call markets where orders accumulate
 * and are matched simultaneously, as opposed to the continuous matching
 * in the standard OrderBook.
 *
 * The quantum approach provides:
 * - Near-optimal solutions for NP-hard matching problems
 * - Exploration of the full combinatorial space via quantum superposition
 * - Graceful fallback to classical greedy for large instances (>20 candidates)
 */

struct QuantumFill {
    std::string bid_order_id;
    std::string ask_order_id;
    double exec_price;
    int64_t quantity;
    double surplus;     // bid_price - ask_price
};

class QuantumMatcher {
public:
    /**
     * Run quantum-optimized batch matching on the current order book state.
     *
     * Takes a snapshot of the book's bids and asks, encodes them as a QUBO
     * problem, runs QAOA to find optimal matching, and returns the fills.
     *
     * Does NOT modify the order book — the caller decides whether to execute.
     */
    static std::vector<QuantumFill> optimize_batch(const OrderBook& book);

    /**
     * Run quantum-optimized matching on explicit bid/ask vectors.
     * Useful for testing or when orders come from multiple sources.
     */
    static std::vector<QuantumFill> optimize(
        const std::vector<Order>& bids,
        const std::vector<Order>& asks
    );

    /**
     * Compare quantum vs classical matching on the same order book.
     * Returns a pair: {quantum_fills, classical_fills}.
     * Useful for benchmarking and validation.
     */
    struct ComparisonResult {
        std::vector<QuantumFill> quantum_fills;
        std::vector<QuantumFill> classical_fills;
        double quantum_surplus;
        double classical_surplus;
    };

    static ComparisonResult compare(const OrderBook& book);
};
