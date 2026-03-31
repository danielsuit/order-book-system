"""
    QuantumRNG - Quantum Random Number Generation via simulated quantum circuits.

    Uses Yao.jl to build circuits of Hadamard gates applied to |0> qubits,
    then measures to produce uniformly random bits. These bits are assembled
    into integers used for gossip peer selection and scheduling jitter.

    The quantum source provides fundamentally non-deterministic randomness
    (in simulation, backed by Julia's Mersenne Twister seeded from OS entropy)
    that cannot resonate with periodic network faults — complementing the
    Wythoff scheduler's quasi-periodic determinism with true stochastic mixing.
"""
module QuantumRNG

using Yao

# Pre-allocated register for reuse (thread-local would be ideal, but
# single-threaded Julia calls from C++ make this safe).
const BATCH_QUBITS = 16  # Generate 16 random bits per circuit evaluation

"""
    random_bits(n::Int) -> Vector{Int}

Generate `n` random bits by running Hadamard circuits on `n` qubits.
Each qubit is initialized to |0>, put into superposition via H gate,
then measured — collapsing to 0 or 1 with equal probability.
"""
function random_bits(n::Int)
    nbatches = cld(n, BATCH_QUBITS)
    bits = Int[]
    sizehint!(bits, n)

    for _ in 1:nbatches
        nq = min(BATCH_QUBITS, n - length(bits))
        # Build circuit: H on each qubit
        circuit = chain(nq, put(nq, i => H) for i in 1:nq)
        reg = zero_state(nq)
        reg |> circuit
        result = measure(reg; nshots=1)
        # Extract individual bits from measurement result
        meas = result[1]  # BitStr
        for i in 1:nq
            push!(bits, (Int(meas) >> (i - 1)) & 1)
        end
    end

    return bits[1:n]
end

"""
    random_uint32() -> UInt32

Generate a 32-bit unsigned random integer from quantum measurement.
"""
function random_uint32()
    bits = random_bits(32)
    val = UInt32(0)
    for i in 1:32
        val |= UInt32(bits[i]) << (i - 1)
    end
    return val
end

"""
    random_uint64() -> UInt64

Generate a 64-bit unsigned random integer from quantum measurement.
"""
function random_uint64()
    lo = UInt64(random_uint32())
    hi = UInt64(random_uint32())
    return (hi << 32) | lo
end

"""
    random_float64() -> Float64

Generate a uniformly distributed float in [0, 1) from quantum bits.
"""
function random_float64()
    return Float64(random_uint64() >> 11) * 2.0^-53
end

"""
    random_peer_index(num_peers::Int) -> Int

Select a random peer index in [0, num_peers-1] using quantum randomness.
Uses rejection sampling for uniform distribution.
"""
function random_peer_index(num_peers::Int)
    if num_peers <= 0
        return 0
    end
    # Rejection sampling to avoid modulo bias
    max_valid = typemax(UInt32) - (typemax(UInt32) % UInt32(num_peers))
    while true
        r = random_uint32()
        if r < max_valid
            return Int(r % UInt32(num_peers))
        end
    end
end

"""
    quantum_jitter(base_interval::Float64) -> Float64

Generate a quantum-random jitter value in [0, base_interval).
Used to decorrelate gossip timing across nodes.
"""
function quantum_jitter(base_interval::Float64)
    return random_float64() * base_interval
end

"""
    generate_quantum_schedule(num_nodes::Int, num_rounds::Int) -> Matrix{Int}

Pre-generate a quantum random gossip schedule.
Returns a (num_rounds x 2) matrix where each row is [sender, receiver].
All indices are 0-based for C++ compatibility.
"""
function generate_quantum_schedule(num_nodes::Int, num_rounds::Int)
    schedule = Matrix{Int}(undef, num_rounds, 2)
    for r in 1:num_rounds
        sender = random_peer_index(num_nodes)
        receiver = random_peer_index(num_nodes - 1)
        if receiver >= sender
            receiver += 1
        end
        schedule[r, 1] = sender
        schedule[r, 2] = receiver
    end
    return schedule
end

end # module QuantumRNG
