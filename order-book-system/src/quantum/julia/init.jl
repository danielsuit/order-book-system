"""
    init.jl - Julia entry point for quantum computing integration.

    This file is loaded by the C++ Julia bridge on startup. It:
    1. Installs Yao.jl if not present
    2. Loads the QuantumRNG and QuantumOptimizer modules
    3. Defines C-callable wrapper functions for the C++ bridge
"""

# Ensure Yao.jl is available
import Pkg
if !haskey(Pkg.project().dependencies, "Yao")
    @info "Installing Yao.jl quantum computing framework..."
    Pkg.add("Yao")
end

# Load quantum modules from the same directory as this file
const QUANTUM_DIR = @__DIR__

include(joinpath(QUANTUM_DIR, "quantum_rng.jl"))
include(joinpath(QUANTUM_DIR, "quantum_optimizer.jl"))

using .QuantumRNG
using .QuantumOptimizer

# ============================================================================
# C-callable bridge functions
# These are called from C++ via jl_call / jl_eval_string
# ============================================================================

"""
    bridge_quantum_peer_index(num_peers::Int) -> Int

C++ bridge: select a random peer using quantum RNG.
Returns 0-based index.
"""
function bridge_quantum_peer_index(num_peers::Int)::Int
    return QuantumRNG.random_peer_index(num_peers)
end

"""
    bridge_quantum_jitter(base_interval::Float64) -> Float64

C++ bridge: generate quantum jitter for gossip timing.
"""
function bridge_quantum_jitter(base_interval::Float64)::Float64
    return QuantumRNG.quantum_jitter(base_interval)
end

"""
    bridge_quantum_uint32() -> UInt32

C++ bridge: generate a quantum random 32-bit integer.
"""
function bridge_quantum_uint32()::UInt32
    return QuantumRNG.random_uint32()
end

"""
    bridge_quantum_schedule(num_nodes::Int, num_rounds::Int) -> Matrix{Int}

C++ bridge: pre-generate a quantum random gossip schedule.
Returns a (num_rounds x 2) matrix of [sender, receiver] pairs (0-based).
"""
function bridge_quantum_schedule(num_nodes::Int, num_rounds::Int)::Matrix{Int}
    return QuantumRNG.generate_quantum_schedule(num_nodes, num_rounds)
end

"""
    bridge_optimize_matching(bids_prices, bids_quantities, asks_prices, asks_quantities)

C++ bridge: run QAOA optimization on order matching.
Returns a matrix where each row is [bid_idx, ask_idx, quantity, exec_price_x1000].
All values are integers for easy C interop (price is multiplied by 1000).
"""
function bridge_optimize_matching(bids_prices::Vector{Float64},
                                   bids_quantities::Vector{Int64},
                                   asks_prices::Vector{Float64},
                                   asks_quantities::Vector{Int64})::Matrix{Int64}
    matches = QuantumOptimizer.optimize_matching(
        bids_prices, bids_quantities,
        asks_prices, asks_quantities;
        p=2, maxiter=50, nshots=512
    )

    if isempty(matches)
        return Matrix{Int64}(undef, 0, 4)
    end

    result = Matrix{Int64}(undef, length(matches), 4)
    for (i, (bi, ai, qty, price)) in enumerate(matches)
        result[i, 1] = bi
        result[i, 2] = ai
        result[i, 3] = qty
        result[i, 4] = round(Int64, price * 10000)  # Fixed-point: 4 decimal places
    end

    return result
end

@info "Quantum computing modules loaded successfully"
@info "  - QuantumRNG: Hadamard-circuit random number generation"
@info "  - QuantumOptimizer: QAOA-based order matching optimization"
