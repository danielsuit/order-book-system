#include "test_framework.h"
#include "quantum/julia_bridge.h"
#include "quantum/quantum_scheduler.h"
#include "quantum/quantum_matcher.h"
#include "orderbook/orderbook.h"

// ============================================================================
// QuantumScheduler tests (work without Julia runtime — tests fallback paths)
// ============================================================================

TEST(quantum_scheduler_pure_fallback) {
    // Without Julia initialized, QuantumScheduler falls back to Wythoff
    QuantumScheduler sched(5, QuantumMode::PURE);

    auto pair = sched.get_pair(1);
    // Should return a valid pair (fallback to Wythoff)
    ASSERT_TRUE(pair.sender < 5);
    ASSERT_TRUE(pair.receiver < 5);
    ASSERT_TRUE(pair.sender != pair.receiver);
}

TEST(quantum_scheduler_hybrid_fallback) {
    QuantumScheduler sched(5, QuantumMode::HYBRID);

    auto pair = sched.get_pair(1);
    ASSERT_TRUE(pair.sender < 5);
    ASSERT_TRUE(pair.receiver < 5);
    ASSERT_TRUE(pair.sender != pair.receiver);
}

TEST(quantum_scheduler_sync_mode_fallback) {
    QuantumScheduler sched(5, QuantumMode::HYBRID);

    // Should return valid sync modes (falls back to Wythoff Beatty sequence)
    int push_count = 0, pull_count = 0;
    for (uint64_t r = 1; r <= 100; ++r) {
        auto mode = sched.get_sync_mode(r);
        if (mode == SyncMode::PUSH) push_count++;
        else pull_count++;
    }
    // Both should be > 0 (Wythoff gives ~38% push, ~62% pull)
    ASSERT_GT(push_count, 0);
    ASSERT_GT(pull_count, 0);
}

TEST(quantum_scheduler_phase_offset_fallback) {
    QuantumScheduler sched(5, QuantumMode::HYBRID);

    double offset = sched.get_phase_offset(2, 100.0);
    ASSERT_TRUE(offset >= 0.0);
    ASSERT_TRUE(offset < 100.0);
}

TEST(quantum_scheduler_coverage_hybrid) {
    QuantumScheduler sched(5, QuantumMode::HYBRID);

    auto matrix = sched.coverage_matrix(100);
    ASSERT_EQ(matrix.size(), 5u);

    // Every node pair should appear at least once in 100 rounds
    for (uint32_t i = 0; i < 5; ++i) {
        for (uint32_t j = 0; j < 5; ++j) {
            if (i != j) {
                ASSERT_GT(matrix[i][j], 0u);
            }
        }
    }
}

// ============================================================================
// JuliaBridge stub tests (without actual Julia runtime)
// ============================================================================

TEST(julia_bridge_not_initialized_by_default) {
    ASSERT_FALSE(JuliaBridge::instance().is_initialized());
}

TEST(julia_bridge_peer_index_returns_zero_when_uninitialized) {
    uint32_t idx = JuliaBridge::instance().quantum_peer_index(5);
    ASSERT_EQ(idx, 0u);
}

TEST(julia_bridge_jitter_returns_zero_when_uninitialized) {
    double jitter = JuliaBridge::instance().quantum_jitter(100.0);
    ASSERT_TRUE(jitter == 0.0);
}

TEST(julia_bridge_schedule_returns_empty_when_uninitialized) {
    auto schedule = JuliaBridge::instance().quantum_schedule(5, 10);
    ASSERT_TRUE(schedule.empty());
}

TEST(julia_bridge_optimize_returns_empty_when_uninitialized) {
    std::vector<double> bp = {100.0, 99.0};
    std::vector<int64_t> bq = {10, 20};
    std::vector<double> ap = {98.0, 97.0};
    std::vector<int64_t> aq = {15, 25};

    auto matches = JuliaBridge::instance().optimize_matching(bp, bq, ap, aq);
    ASSERT_TRUE(matches.empty());
}

// ============================================================================
// QuantumMatcher tests (fallback behavior without Julia)
// ============================================================================

TEST(quantum_matcher_empty_book) {
    OrderBook book;
    auto fills = QuantumMatcher::optimize_batch(book);
    ASSERT_TRUE(fills.empty());
}

TEST(quantum_matcher_compare_empty_book) {
    OrderBook book;
    auto result = QuantumMatcher::compare(book);
    ASSERT_TRUE(result.quantum_fills.empty());
    ASSERT_TRUE(result.classical_fills.empty());
    ASSERT_TRUE(result.quantum_surplus == 0.0);
    ASSERT_TRUE(result.classical_surplus == 0.0);
}

TEST(quantum_matcher_compare_with_orders) {
    OrderBook book;

    // Add some bids
    Order bid1;
    bid1.order_id = "b1"; bid1.node_id = "n1"; bid1.side = Side::BUY;
    bid1.price = 100.0; bid1.quantity = 10; bid1.remaining = 10; bid1.timestamp = 1;
    book.add_order(bid1);

    Order bid2;
    bid2.order_id = "b2"; bid2.node_id = "n1"; bid2.side = Side::BUY;
    bid2.price = 99.0; bid2.quantity = 20; bid2.remaining = 20; bid2.timestamp = 2;
    book.add_order(bid2);

    // Add some asks
    Order ask1;
    ask1.order_id = "a1"; ask1.node_id = "n1"; ask1.side = Side::SELL;
    ask1.price = 98.0; ask1.quantity = 15; ask1.remaining = 15; ask1.timestamp = 3;
    book.add_order(ask1);

    Order ask2;
    ask2.order_id = "a2"; ask2.node_id = "n1"; ask2.side = Side::SELL;
    ask2.price = 97.0; ask2.quantity = 25; ask2.remaining = 25; ask2.timestamp = 4;
    book.add_order(ask2);

    // Without Julia, quantum fills will be empty but classical should work
    auto result = QuantumMatcher::compare(book);

    // Classical greedy should find matches (bid >= ask)
    ASSERT_GT(result.classical_fills.size(), 0u);
    ASSERT_GT(result.classical_surplus, 0.0);
}
