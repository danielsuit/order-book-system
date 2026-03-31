#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include "../gossip/wythoff_scheduler.h"
#include "julia_bridge.h"

/**
 * QuantumScheduler — Gossip peer selection using quantum random numbers.
 *
 * Combines the Wythoff scheduler's deterministic coverage guarantees with
 * quantum-random perturbation. Two modes:
 *
 *  QUANTUM_PURE:  Peer selection is entirely quantum-random. Each round
 *                 draws a fresh peer index from the Hadamard circuit RNG.
 *                 Advantages: impossible for periodic network faults to
 *                 correlate with the schedule. Disadvantage: no coverage
 *                 guarantee over short windows.
 *
 *  QUANTUM_HYBRID: Uses Wythoff as the base schedule but applies quantum
 *                  jitter to gossip timing and occasionally (with quantum
 *                  probability) swaps the Wythoff peer for a quantum-random
 *                  one. This preserves Wythoff's coverage properties while
 *                  injecting enough randomness to break fault correlations.
 */
enum class QuantumMode {
    PURE,    // Fully quantum-random peer selection
    HYBRID   // Wythoff base + quantum perturbation
};

class QuantumScheduler {
public:
    QuantumScheduler(uint32_t num_nodes, QuantumMode mode = QuantumMode::HYBRID);

    // Get the gossip pair for a given round
    GossipPair get_pair(uint64_t round) const;

    // Get sync mode (push/pull) — hybrid uses Wythoff's Beatty sequence,
    // pure uses quantum coin flip
    SyncMode get_sync_mode(uint64_t round) const;

    // Get quantum-jittered phase offset for a node
    double get_phase_offset(uint32_t node_id, double interval) const;

    // Pre-generate a quantum schedule batch for performance
    // (Amortizes Julia call overhead over many rounds)
    void prefetch_schedule(uint64_t start_round, uint64_t count);

    // Coverage analysis (delegates to Wythoff in hybrid mode,
    // runs quantum simulation in pure mode)
    std::vector<std::vector<uint32_t>> coverage_matrix(uint64_t num_rounds) const;

    QuantumMode mode() const { return mode_; }

private:
    uint32_t num_nodes_;
    QuantumMode mode_;

    // Wythoff scheduler for hybrid mode
    WythoffScheduler wythoff_;

    // Prefetched quantum schedule
    mutable std::mutex cache_mutex_;
    uint64_t cache_start_ = 0;
    std::vector<JuliaBridge::GossipPairQ> cached_schedule_;

    // Quantum swap probability in hybrid mode (0.0 to 1.0)
    // Higher = more randomness, lower = more Wythoff coverage
    static constexpr double QUANTUM_SWAP_PROB = 0.2;
};
