"""
    QuantumOptimizer - QAOA-based order matching optimization.

    Uses the Quantum Approximate Optimization Algorithm (QAOA) to find
    optimal matchings between bids and asks in the order book. The problem
    is encoded as a MaxCut-style combinatorial optimization where:

    - Each potential (bid, ask) pair is a binary decision variable
    - The objective maximizes total matched volume weighted by price improvement
    - Constraints enforce that each order is matched at most once

    This is a quantum-classical hybrid: the quantum circuit explores the
    solution space via parameterized rotations, and a classical optimizer
    (gradient-free Nelder-Mead) tunes the circuit parameters.
"""
module QuantumOptimizer

using Yao
using Yao.EasyBuild
using LinearAlgebra

"""
    MatchCandidate

A potential match between a bid and an ask.
"""
struct MatchCandidate
    bid_idx::Int        # Index into bids array (0-based for C++)
    ask_idx::Int        # Index into asks array (0-based for C++)
    price::Float64      # Execution price (midpoint)
    quantity::Int64     # Matchable quantity (min of both sides)
    surplus::Float64    # Price improvement: bid_price - ask_price
end

"""
    encode_cost_hamiltonian(candidates::Vector{MatchCandidate}, num_bids::Int, num_asks::Int)

Build the cost Hamiltonian for the matching problem.

The cost function is:
    C(x) = sum_i surplus_i * quantity_i * x_i
           - penalty * sum_constraints_violated

where x_i in {0,1} indicates whether candidate i is selected,
and constraints ensure each bid/ask is matched at most once.
"""
function encode_cost_hamiltonian(candidates::Vector{MatchCandidate},
                                  num_bids::Int, num_asks::Int)
    n = length(candidates)
    if n == 0
        return nothing
    end

    # Diagonal cost matrix (QUBO formulation)
    # Maximize: sum(surplus * qty * x_i)
    # Penalize: pairs sharing a bid or ask being simultaneously selected
    penalty = 0.0
    for c in candidates
        penalty = max(penalty, abs(c.surplus * c.quantity))
    end
    penalty *= 2.0  # Penalty must dominate any single term

    # Build QUBO matrix Q where objective = x^T Q x
    Q = zeros(Float64, n, n)

    # Linear terms (diagonal)
    for i in 1:n
        Q[i, i] = candidates[i].surplus * candidates[i].quantity
    end

    # Penalty terms for constraint violations (off-diagonal)
    for i in 1:n
        for j in (i+1):n
            # Same bid constraint
            if candidates[i].bid_idx == candidates[j].bid_idx
                Q[i, j] -= penalty
                Q[j, i] -= penalty
            end
            # Same ask constraint
            if candidates[i].ask_idx == candidates[j].ask_idx
                Q[i, j] -= penalty
                Q[j, i] -= penalty
            end
        end
    end

    return Q
end

"""
    build_qaoa_circuit(Q::Matrix{Float64}, gamma::Vector{Float64}, beta::Vector{Float64})

Build a p-layer QAOA circuit for the given QUBO matrix.

Layer structure:
  1. Initialize in uniform superposition (H^n)
  2. For each layer k:
     a. Problem unitary: exp(-i * gamma_k * C) via ZZ and Z rotations
     b. Mixer unitary: exp(-i * beta_k * B) via X rotations
"""
function build_qaoa_circuit(Q::Matrix{Float64}, gamma::Vector{Float64}, beta::Vector{Float64})
    n = size(Q, 1)
    p = length(gamma)  # Number of QAOA layers

    # Start with Hadamard layer
    circuit = chain(n, put(n, i => H) for i in 1:n)

    for layer in 1:p
        g = gamma[layer]
        b = beta[layer]

        # Problem unitary: diagonal Z rotations from Q
        problem_layer = chain(n)

        # Single-qubit Z rotations from diagonal of Q
        for i in 1:n
            if abs(Q[i, i]) > 1e-12
                push!(problem_layer, put(n, i => Rz(2 * g * Q[i, i])))
            end
        end

        # Two-qubit ZZ interactions from off-diagonal of Q
        for i in 1:n
            for j in (i+1):n
                coupling = Q[i, j] + Q[j, i]
                if abs(coupling) > 1e-12
                    # ZZ interaction via CNOT-Rz-CNOT decomposition
                    push!(problem_layer, cnot(n, i, j))
                    push!(problem_layer, put(n, j => Rz(2 * g * coupling)))
                    push!(problem_layer, cnot(n, i, j))
                end
            end
        end

        # Mixer unitary: X rotations on each qubit
        mixer_layer = chain(n, put(n, i => Rx(2 * b)) for i in 1:n)

        circuit = chain(circuit, problem_layer, mixer_layer)
    end

    return circuit
end

"""
    evaluate_qaoa(Q::Matrix{Float64}, gamma::Vector{Float64}, beta::Vector{Float64};
                  nshots::Int=1024) -> Float64

Evaluate the expected cost of a QAOA circuit with given parameters.
Returns the negative expectation value (for minimization).
"""
function evaluate_qaoa(Q::Matrix{Float64}, gamma::Vector{Float64}, beta::Vector{Float64};
                       nshots::Int=1024)
    n = size(Q, 1)
    circuit = build_qaoa_circuit(Q, gamma, beta)

    reg = zero_state(n)
    reg |> circuit

    # Sample and compute average cost
    measurements = measure(reg; nshots=nshots)
    total_cost = 0.0
    for meas in measurements
        bits = Int(meas)
        cost = 0.0
        for i in 1:n
            xi = (bits >> (i - 1)) & 1
            cost += Q[i, i] * xi
            for j in (i+1):n
                xj = (bits >> (j - 1)) & 1
                cost += (Q[i, j] + Q[j, i]) * xi * xj
            end
        end
        total_cost += cost
    end

    return -total_cost / nshots  # Negative because we maximize
end

"""
    nelder_mead_optimize(f, x0::Vector{Float64}; maxiter::Int=100, tol::Float64=1e-6)

Simple Nelder-Mead optimizer for tuning QAOA parameters.
No external dependency needed.
"""
function nelder_mead_optimize(f, x0::Vector{Float64}; maxiter::Int=100, tol::Float64=1e-6)
    n = length(x0)
    alpha = 1.0   # Reflection
    gamma_nm = 2.0  # Expansion
    rho = 0.5     # Contraction
    sigma = 0.5   # Shrink

    # Initialize simplex
    simplex = [copy(x0)]
    for i in 1:n
        xi = copy(x0)
        xi[i] += 0.5
        push!(simplex, xi)
    end
    fvals = [f(s) for s in simplex]

    for _ in 1:maxiter
        # Sort
        order = sortperm(fvals)
        simplex = simplex[order]
        fvals = fvals[order]

        # Check convergence
        if maximum(abs.(fvals[end] - fvals[1])) < tol
            break
        end

        # Centroid (excluding worst)
        centroid = mean(simplex[1:end-1])

        # Reflection
        xr = centroid + alpha * (centroid - simplex[end])
        fr = f(xr)

        if fvals[1] <= fr < fvals[end-1]
            simplex[end] = xr
            fvals[end] = fr
        elseif fr < fvals[1]
            # Expansion
            xe = centroid + gamma_nm * (xr - centroid)
            fe = f(xe)
            if fe < fr
                simplex[end] = xe
                fvals[end] = fe
            else
                simplex[end] = xr
                fvals[end] = fr
            end
        else
            # Contraction
            xc = centroid + rho * (simplex[end] - centroid)
            fc = f(xc)
            if fc < fvals[end]
                simplex[end] = xc
                fvals[end] = fc
            else
                # Shrink
                for i in 2:length(simplex)
                    simplex[i] = simplex[1] + sigma * (simplex[i] - simplex[1])
                    fvals[i] = f(simplex[i])
                end
            end
        end
    end

    best_idx = argmin(fvals)
    return simplex[best_idx], fvals[best_idx]
end

"""
    optimize_matching(bids_prices::Vector{Float64}, bids_quantities::Vector{Int64},
                      asks_prices::Vector{Float64}, asks_quantities::Vector{Int64};
                      p::Int=2, maxiter::Int=50, nshots::Int=512) -> Vector{Tuple{Int,Int,Int64,Float64}}

Main entry point: find optimal order matching using QAOA.

Returns a vector of (bid_idx, ask_idx, quantity, exec_price) tuples (0-based indices).
Falls back to classical greedy matching if the problem is too large for quantum simulation.
"""
function optimize_matching(bids_prices::Vector{Float64}, bids_quantities::Vector{Int64},
                           asks_prices::Vector{Float64}, asks_quantities::Vector{Int64};
                           p::Int=2, maxiter::Int=50, nshots::Int=512)

    num_bids = length(bids_prices)
    num_asks = length(asks_prices)

    # Build candidate matches (only where bid >= ask, i.e., positive surplus)
    candidates = MatchCandidate[]
    for i in 1:num_bids
        for j in 1:num_asks
            surplus = bids_prices[i] - asks_prices[j]
            if surplus >= 0
                qty = min(bids_quantities[i], asks_quantities[j])
                exec_price = (bids_prices[i] + asks_prices[j]) / 2.0
                push!(candidates, MatchCandidate(i - 1, j - 1, exec_price, qty, surplus))
            end
        end
    end

    if isempty(candidates)
        return Tuple{Int,Int,Int64,Float64}[]
    end

    n_candidates = length(candidates)

    # Quantum simulation is practical up to ~20 qubits
    if n_candidates > 20
        return _classical_greedy_matching(candidates, num_bids, num_asks)
    end

    # Build QUBO
    Q = encode_cost_hamiltonian(candidates, num_bids, num_asks)
    if Q === nothing
        return Tuple{Int,Int,Int64,Float64}[]
    end

    # Optimize QAOA parameters
    initial_params = vcat(fill(0.5, p), fill(0.5, p))  # [gamma..., beta...]

    function cost_fn(params)
        gamma = params[1:p]
        beta = params[p+1:2p]
        return evaluate_qaoa(Q, gamma, beta; nshots=nshots)
    end

    best_params, _ = nelder_mead_optimize(cost_fn, initial_params;
                                           maxiter=maxiter, tol=1e-4)

    # Sample the optimized circuit for the best solution
    best_gamma = best_params[1:p]
    best_beta = best_params[p+1:2p]
    circuit = build_qaoa_circuit(Q, best_gamma, best_beta)

    reg = zero_state(n_candidates)
    reg |> circuit
    measurements = measure(reg; nshots=nshots * 4)

    # Find the best feasible solution
    best_cost = -Inf
    best_bits = 0
    for meas in measurements
        bits = Int(meas)
        cost = _evaluate_solution(bits, Q, candidates, num_bids, num_asks)
        if cost > best_cost
            best_cost = cost
            best_bits = bits
        end
    end

    # Extract matches from best bitstring
    matches = Tuple{Int,Int,Int64,Float64}[]
    used_bids = Set{Int}()
    used_asks = Set{Int}()

    for i in 1:n_candidates
        if ((best_bits >> (i - 1)) & 1) == 1
            c = candidates[i]
            if !(c.bid_idx in used_bids) && !(c.ask_idx in used_asks)
                push!(matches, (c.bid_idx, c.ask_idx, c.quantity, c.price))
                push!(used_bids, c.bid_idx)
                push!(used_asks, c.ask_idx)
            end
        end
    end

    return matches
end

"""
Evaluate a candidate solution, returning -Inf if constraints are violated.
"""
function _evaluate_solution(bits::Int, Q::Matrix{Float64},
                            candidates::Vector{MatchCandidate},
                            num_bids::Int, num_asks::Int)
    n = length(candidates)
    used_bids = Set{Int}()
    used_asks = Set{Int}()

    # Check feasibility
    for i in 1:n
        if ((bits >> (i - 1)) & 1) == 1
            c = candidates[i]
            if c.bid_idx in used_bids || c.ask_idx in used_asks
                return -Inf  # Constraint violated
            end
            push!(used_bids, c.bid_idx)
            push!(used_asks, c.ask_idx)
        end
    end

    # Compute objective value
    cost = 0.0
    for i in 1:n
        xi = (bits >> (i - 1)) & 1
        cost += candidates[i].surplus * candidates[i].quantity * xi
    end

    return cost
end

"""
Classical greedy fallback for large problem instances.
Sorts candidates by surplus*quantity descending and greedily selects.
"""
function _classical_greedy_matching(candidates::Vector{MatchCandidate},
                                     num_bids::Int, num_asks::Int)
    sorted = sort(candidates, by=c -> c.surplus * c.quantity, rev=true)
    used_bids = Set{Int}()
    used_asks = Set{Int}()
    matches = Tuple{Int,Int,Int64,Float64}[]

    for c in sorted
        if !(c.bid_idx in used_bids) && !(c.ask_idx in used_asks)
            push!(matches, (c.bid_idx, c.ask_idx, c.quantity, c.price))
            push!(used_bids, c.bid_idx)
            push!(used_asks, c.ask_idx)
        end
    end

    return matches
end

end # module QuantumOptimizer
