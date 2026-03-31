#include "quantum_scheduler.h"

static constexpr uint64_t PREFETCH_BATCH_SIZE = 256;

QuantumScheduler::QuantumScheduler(uint32_t num_nodes, QuantumMode mode)
    : num_nodes_(num_nodes), mode_(mode), wythoff_(num_nodes) {}

GossipPair QuantumScheduler::get_pair(uint64_t round) const {
    auto& bridge = JuliaBridge::instance();

    if (!bridge.is_initialized()) {
        // Fallback to Wythoff if Julia isn't available
        return wythoff_.get_pair(round);
    }

    if (mode_ == QuantumMode::PURE) {
        // Check prefetched cache first
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            if (round >= cache_start_ &&
                round < cache_start_ + cached_schedule_.size()) {
                auto& qp = cached_schedule_[round - cache_start_];
                return {qp.sender, qp.receiver};
            }
        }

        // Cache miss — generate directly from quantum RNG
        uint32_t sender = bridge.quantum_peer_index(num_nodes_);
        uint32_t receiver = bridge.quantum_peer_index(num_nodes_ - 1);
        if (receiver >= sender) receiver++;
        return {sender, receiver};
    }

    // HYBRID mode: Wythoff base with quantum perturbation
    GossipPair base_pair = wythoff_.get_pair(round);

    // Quantum coin flip: swap peer with probability QUANTUM_SWAP_PROB
    uint32_t coin = bridge.quantum_uint32();
    double prob = static_cast<double>(coin) / static_cast<double>(UINT32_MAX);

    if (prob < QUANTUM_SWAP_PROB) {
        // Replace with quantum-random peer
        uint32_t new_receiver = bridge.quantum_peer_index(num_nodes_ - 1);
        if (new_receiver >= base_pair.sender) new_receiver++;
        base_pair.receiver = new_receiver;
    }

    return base_pair;
}

SyncMode QuantumScheduler::get_sync_mode(uint64_t round) const {
    auto& bridge = JuliaBridge::instance();

    if (mode_ == QuantumMode::HYBRID || !bridge.is_initialized()) {
        // Use Wythoff's Beatty sequence for structured push/pull ratio
        return wythoff_.get_sync_mode(round);
    }

    // Pure quantum: coin flip with bias toward PULL (matching Wythoff's 1:phi ratio)
    // P(PULL) = 1/phi ≈ 0.618
    uint32_t coin = bridge.quantum_uint32();
    double prob = static_cast<double>(coin) / static_cast<double>(UINT32_MAX);
    static constexpr double PULL_PROB = 1.0 / WythoffScheduler::PHI;  // ~0.618

    return (prob < PULL_PROB) ? SyncMode::PULL : SyncMode::PUSH;
}

double QuantumScheduler::get_phase_offset(uint32_t node_id, double interval) const {
    auto& bridge = JuliaBridge::instance();

    if (!bridge.is_initialized()) {
        return wythoff_.get_phase_offset(node_id, interval);
    }

    if (mode_ == QuantumMode::HYBRID) {
        // Wythoff base offset + quantum jitter (±10% of interval)
        double base = wythoff_.get_phase_offset(node_id, interval);
        double jitter = bridge.quantum_jitter(interval * 0.2) - (interval * 0.1);
        double result = base + jitter;
        if (result < 0) result += interval;
        if (result >= interval) result -= interval;
        return result;
    }

    // Pure quantum jitter
    return bridge.quantum_jitter(interval);
}

void QuantumScheduler::prefetch_schedule(uint64_t start_round, uint64_t count) {
    auto& bridge = JuliaBridge::instance();
    if (!bridge.is_initialized()) return;

    auto schedule = bridge.quantum_schedule(num_nodes_, static_cast<int>(count));

    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_start_ = start_round;
    cached_schedule_ = std::move(schedule);
}

std::vector<std::vector<uint32_t>> QuantumScheduler::coverage_matrix(
        uint64_t num_rounds) const {
    if (mode_ == QuantumMode::HYBRID) {
        // In hybrid mode, coverage is mostly Wythoff's
        return wythoff_.coverage_matrix(num_rounds);
    }

    // Pure quantum: simulate schedule
    std::vector<std::vector<uint32_t>> matrix(
        num_nodes_, std::vector<uint32_t>(num_nodes_, 0));

    for (uint64_t r = 1; r <= num_rounds; ++r) {
        auto pair = get_pair(r);
        matrix[pair.sender][pair.receiver]++;
    }

    return matrix;
}
