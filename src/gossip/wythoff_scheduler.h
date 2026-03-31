#pragma once
#include <cstdint>
#include <vector>
#include <cmath>

struct GossipPair {
    uint32_t sender;
    uint32_t receiver;
};

enum class SyncMode {
    PUSH,  // sender pushes its state to receiver
    PULL   // sender requests state from receiver
};

class WythoffScheduler {
public:
    // phi and phi^2 as constants
    static constexpr double PHI = 1.6180339887498948482;
    static constexpr double PHI_SQ = 2.6180339887498948482;

    explicit WythoffScheduler(uint32_t num_nodes);

    // Core: get the gossip pair for a given round
    GossipPair get_pair(uint64_t round) const;

    // Determine push vs pull using Beatty sequence membership
    SyncMode get_sync_mode(uint64_t round) const;

    // Get the anti-entropy phase offset for a node (golden-angle jitter)
    // Returns a value in [0, interval) that this node should delay before syncing
    double get_phase_offset(uint32_t node_id, double interval) const;

    // Precompute schedule for N rounds (useful for testing/visualization)
    std::vector<GossipPair> precompute_schedule(uint64_t start_round, uint64_t count) const;

    // Coverage analysis: count how many times each node pair appears in N rounds
    // Returns a num_nodes x num_nodes matrix
    std::vector<std::vector<uint32_t>> coverage_matrix(uint64_t num_rounds) const;

private:
    uint32_t num_nodes_;

    // Beatty sequence membership test:
    // n is in sequence {floor(k*phi)} iff round(n / phi) * phi rounded == n
    bool is_in_beatty_phi(uint64_t n) const;
};
