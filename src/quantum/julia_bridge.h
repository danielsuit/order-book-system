#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>

#ifndef CONVERGENCE_QUANTUM_ENABLED
#define CONVERGENCE_QUANTUM_ENABLED 0
#endif

#if CONVERGENCE_QUANTUM_ENABLED
// Forward-declare Julia types to avoid requiring julia.h in all translation units
struct _jl_value_t;
typedef _jl_value_t jl_value_t;
#else
typedef void jl_value_t;
#endif

struct QuantumMatch {
    int bid_idx;
    int ask_idx;
    int64_t quantity;
    double exec_price;
};

/**
 * JuliaBridge — Embeds the Julia runtime and provides a C++ interface
 * to the quantum computing modules (QuantumRNG and QuantumOptimizer).
 *
 * Thread-safety: All calls into Julia are serialized through a mutex
 * because Julia's runtime is single-threaded. The bridge caches
 * function handles after first lookup for performance.
 *
 * Lifecycle:
 *   1. Call JuliaBridge::instance().initialize(julia_dir) once at startup
 *   2. Use quantum_peer_index(), quantum_jitter(), optimize_matching() freely
 *   3. Call JuliaBridge::instance().shutdown() before exit
 *
 * When compiled without Julia (CONVERGENCE_QUANTUM_ENABLED=0), all methods
 * are safe to call but return default values and initialize() returns false.
 */
class JuliaBridge {
public:
    static JuliaBridge& instance();

    // Initialize Julia runtime and load quantum modules.
    // julia_scripts_dir: path to the directory containing init.jl
    bool initialize(const std::string& julia_scripts_dir);

    // Shutdown Julia runtime. Must be called before process exit.
    void shutdown();

    bool is_initialized() const { return initialized_.load(); }

    // --- QuantumRNG interface ---

    // Generate a random peer index in [0, num_peers) using quantum RNG
    uint32_t quantum_peer_index(int num_peers);

    // Generate quantum jitter in [0, base_interval)
    double quantum_jitter(double base_interval);

    // Generate a quantum random uint32
    uint32_t quantum_uint32();

    // Pre-generate a quantum gossip schedule
    // Returns vector of (sender, receiver) pairs (0-based)
    struct GossipPairQ { uint32_t sender; uint32_t receiver; };
    std::vector<GossipPairQ> quantum_schedule(int num_nodes, int num_rounds);

    // --- QuantumOptimizer interface ---

    // Run QAOA optimization on order matching
    std::vector<QuantumMatch> optimize_matching(
        const std::vector<double>& bids_prices,
        const std::vector<int64_t>& bids_quantities,
        const std::vector<double>& asks_prices,
        const std::vector<int64_t>& asks_quantities
    );

private:
    JuliaBridge() = default;
    ~JuliaBridge();

    JuliaBridge(const JuliaBridge&) = delete;
    JuliaBridge& operator=(const JuliaBridge&) = delete;

#if CONVERGENCE_QUANTUM_ENABLED
    // Helper: call a Julia function by name with arguments
    jl_value_t* call_julia(const std::string& func_name,
                           jl_value_t** args, int nargs);

    // Helper: convert C++ vector to Julia array
    jl_value_t* to_julia_float64_array(const std::vector<double>& vec);
    jl_value_t* to_julia_int64_array(const std::vector<int64_t>& vec);

    // Cached function handles
    jl_value_t* fn_peer_index_ = nullptr;
    jl_value_t* fn_jitter_ = nullptr;
    jl_value_t* fn_uint32_ = nullptr;
    jl_value_t* fn_schedule_ = nullptr;
    jl_value_t* fn_optimize_ = nullptr;
#endif

    std::atomic<bool> initialized_{false};
    std::mutex julia_mutex_;
};
