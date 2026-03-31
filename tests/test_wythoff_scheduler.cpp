#include "test_framework.h"
#include "gossip/wythoff_scheduler.h"
#include <set>
#include <algorithm>
#include <cmath>

TEST(beatty_sequence_partition) {
    // For rounds 1..1000, classify each as PUSH or PULL.
    // The two Beatty sequences partition the positive integers, so every
    // round must be exactly one of PUSH or PULL.
    // Expected ratio: PUSH ~ 1000/phi^2 ~ 382, PULL ~ 1000/phi ~ 618.
    WythoffScheduler scheduler(5);

    int push_count = 0;
    int pull_count = 0;
    for (uint64_t r = 1; r <= 1000; ++r) {
        SyncMode mode = scheduler.get_sync_mode(r);
        if (mode == SyncMode::PUSH) push_count++;
        else pull_count++;
    }

    // Every round is exactly one
    ASSERT_EQ(push_count + pull_count, 1000);

    // PUSH count ~ 382 (within +-5)
    ASSERT_TRUE(push_count >= 377);
    ASSERT_TRUE(push_count <= 387);

    // PULL count ~ 618 (within +-5)
    ASSERT_TRUE(pull_count >= 613);
    ASSERT_TRUE(pull_count <= 623);

    // PULL > PUSH (pull-biased)
    ASSERT_GT(pull_count, push_count);
}

TEST(pair_coverage_uniformity) {
    // Run 10000 rounds with 5 nodes. Every ordered pair (i,j) where i!=j
    // should appear roughly 10000/20 = 500 times, within 25%.
    WythoffScheduler scheduler(5);
    auto matrix = scheduler.coverage_matrix(10000);

    uint32_t num_nodes = 5;
    double expected = 10000.0 / (num_nodes * (num_nodes - 1));  // 500

    for (uint32_t i = 0; i < num_nodes; ++i) {
        for (uint32_t j = 0; j < num_nodes; ++j) {
            if (i == j) continue;
            double count = static_cast<double>(matrix[i][j]);
            ASSERT_GT(count, expected * 0.75);
            ASSERT_TRUE(count < expected * 1.25);
        }
    }
}

TEST(pair_determinism) {
    // Same scheduler, same round -> same pair
    WythoffScheduler s1(5);
    auto p1a = s1.get_pair(42);
    auto p1b = s1.get_pair(42);
    ASSERT_EQ(p1a.sender, p1b.sender);
    ASSERT_EQ(p1a.receiver, p1b.receiver);

    // Different scheduler instance, same num_nodes -> same pair
    WythoffScheduler s2(5);
    auto p2 = s2.get_pair(42);
    ASSERT_EQ(p1a.sender, p2.sender);
    ASSERT_EQ(p1a.receiver, p2.receiver);

    // Also check sync mode is deterministic
    ASSERT_EQ(static_cast<int>(s1.get_sync_mode(42)),
              static_cast<int>(s2.get_sync_mode(42)));
}

TEST(no_self_gossip) {
    // For rounds 1..10000 with 5 nodes, sender must never equal receiver.
    WythoffScheduler scheduler(5);
    for (uint64_t r = 1; r <= 10000; ++r) {
        auto pair = scheduler.get_pair(r);
        ASSERT_NE(pair.sender, pair.receiver);
    }
}

TEST(phase_offset_spread) {
    // With 5 nodes and interval=1000ms, golden-angle spacing should
    // produce well-spread offsets. Minimum gap must be > interval/(num_nodes*2).
    WythoffScheduler scheduler(5);
    double interval = 1000.0;
    uint32_t num_nodes = 5;
    double min_gap_threshold = interval / (num_nodes * 2);  // 100ms

    std::vector<double> offsets;
    for (uint32_t i = 0; i < num_nodes; ++i) {
        offsets.push_back(scheduler.get_phase_offset(i, interval));
    }
    std::sort(offsets.begin(), offsets.end());

    // Check gaps between consecutive offsets (including wrap-around)
    for (size_t i = 0; i < offsets.size(); ++i) {
        double next = (i + 1 < offsets.size()) ? offsets[i + 1] : offsets[0] + interval;
        double gap = next - offsets[i];
        ASSERT_GT(gap, min_gap_threshold);
    }

    // All offsets should be in [0, interval)
    for (double o : offsets) {
        ASSERT_TRUE(o >= 0.0);
        ASSERT_TRUE(o < interval);
    }
}

TEST(quasi_periodicity) {
    // The Wythoff schedule must NOT be periodic with any period P <= 50.
    // For each candidate period P, at least one pair in 1..100 must differ
    // from the pair at offset P.
    WythoffScheduler scheduler(5);
    auto schedule = scheduler.precompute_schedule(1, 100);

    for (uint64_t period = 1; period <= 50; ++period) {
        bool found_difference = false;
        for (uint64_t i = 0; i + period < 100; ++i) {
            if (schedule[i].sender != schedule[i + period].sender ||
                schedule[i].receiver != schedule[i + period].receiver) {
                found_difference = true;
                break;
            }
        }
        ASSERT_TRUE(found_difference);
    }
}
