#include "wythoff_scheduler.h"

WythoffScheduler::WythoffScheduler(uint32_t num_nodes)
    : num_nodes_(num_nodes) {}

GossipPair WythoffScheduler::get_pair(uint64_t round) const {
    // Use floor(round * phi) to produce a quasi-periodic index into the set
    // of all N*(N-1) directed node pairs (excluding self-pairs).
    // This gives uniform coverage because phi is irrational: the sequence
    // {floor(r*phi) mod M} is equidistributed for any M.
    uint32_t num_pairs = num_nodes_ * (num_nodes_ - 1);
    auto wythoff_val = static_cast<uint64_t>(std::floor(round * PHI));
    uint32_t pair_idx = static_cast<uint32_t>(wythoff_val % num_pairs);

    // Decode pair_idx into (sender, receiver), skipping the diagonal
    uint32_t sender = pair_idx / (num_nodes_ - 1);
    uint32_t receiver = pair_idx % (num_nodes_ - 1);
    if (receiver >= sender) receiver++;

    return {sender, receiver};
}

SyncMode WythoffScheduler::get_sync_mode(uint64_t round) const {
    // First Beatty sequence {floor(k*phi)} has density 1/phi ~ 0.618 → PULL (larger set)
    // Second Beatty sequence {floor(k*phi^2)} has density 1/phi^2 ~ 0.382 → PUSH (smaller set)
    // This gives a push:pull ratio of 1:phi, favoring pull for faster convergence.
    return is_in_beatty_phi(round) ? SyncMode::PULL : SyncMode::PUSH;
}

double WythoffScheduler::get_phase_offset(uint32_t node_id, double interval) const {
    return std::fmod(node_id * interval / PHI, interval);
}

std::vector<GossipPair> WythoffScheduler::precompute_schedule(uint64_t start_round, uint64_t count) const {
    std::vector<GossipPair> schedule;
    schedule.reserve(count);
    for (uint64_t r = start_round; r < start_round + count; ++r) {
        schedule.push_back(get_pair(r));
    }
    return schedule;
}

std::vector<std::vector<uint32_t>> WythoffScheduler::coverage_matrix(uint64_t num_rounds) const {
    std::vector<std::vector<uint32_t>> matrix(num_nodes_, std::vector<uint32_t>(num_nodes_, 0));
    for (uint64_t r = 1; r <= num_rounds; ++r) {
        auto pair = get_pair(r);
        matrix[pair.sender][pair.receiver]++;
    }
    return matrix;
}

bool WythoffScheduler::is_in_beatty_phi(uint64_t n) const {
    // n is in the first Beatty sequence {floor(k * phi) : k >= 1} iff
    // there exists integer k >= 1 such that floor(k * phi) == n.
    // Find the candidate k = ceil(n / phi), then verify floor(k * phi) == n.
    static constexpr double INV_PHI = 1.0 / PHI;
    uint64_t k = static_cast<uint64_t>(std::ceil(static_cast<double>(n) * INV_PHI));
    return static_cast<uint64_t>(std::floor(static_cast<double>(k) * PHI)) == n;
}
