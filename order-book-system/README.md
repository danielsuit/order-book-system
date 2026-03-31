# Convergence — Distributed Order Book

A CRDT-based distributed order book system with gossip replication across 5 Docker nodes, a React frontend dashboard, quantum-enhanced scheduling via Julia/Yao.jl, and Prometheus/Grafana monitoring.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Frontend (React + Vite)          localhost:5173             │
│  ┌────────┬────────┬──────────┬──────────┬───────────────┐   │
│  │OrderBook│DepthChart│PriceChart│OrderEntry│ ActivityLog │   │
│  └────┬───┴────┬───┴────┬─────┴────┬─────┴──────┬───────┘   │
│       └────────┴────────┴──────────┴────────────┘            │
│                         │ WebSocket                          │
│                    ┌────▼─────┐                               │
│                    │  Bridge  │  Node.js WS server :3001      │
│                    └────┬─────┘                               │
│           TCP ┌─────────┼─────────┐                          │
│        ┌──────▼──┐ ┌────▼───┐ ┌───▼─────┐                   │
│        │ node-a  │ │ node-b │ │ node-c  │ ... node-d, node-e│
│        │  AAPL   │ │  MSFT  │ │  NVDA   │     AMZN    GOOG  │
│        └────┬────┘ └───┬────┘ └───┬─────┘                   │
│             │   UDP Gossip Protocol│                          │
│             └──────────┼──────────┘                           │
│                        │                                     │
│  ┌─────────────────────▼─────────────────────────┐           │
│  │         Quantum Computing Layer (Julia)        │           │
│  │  QuantumRNG (Yao.jl)  │  QAOA Optimizer       │           │
│  └───────────────────────────────────────────────┘           │
│                                                              │
│  Monitoring: Prometheus :9090  │  Grafana :3000              │
└──────────────────────────────────────────────────────────────┘
```

### Core Components

**Order Book Engine** (`src/orderbook/`) — Standard limit order book with price-time priority. Supports limit orders (BUY/SELL), cancellation, and market orders. Bids sorted descending, asks sorted ascending via `std::map`.

**CRDT Replication** (`src/crdt/`) — Each order book is wrapped in a CRDT layer that maintains a vector clock and operation log. Operations (ADD_ORDER, CANCEL_ORDER) are replicated to peer nodes and applied idempotently. Conflicts are resolved via Lamport timestamps with node_id tiebreaker.

**Gossip Protocol** (`src/gossip/`) — Nodes exchange operations via UDP gossip with configurable peer selection strategies:

| Strategy | How it works |
|---|---|
| `RANDOM` | Pick a random alive peer each round |
| `ROUND_ROBIN` | Cycle through peers sequentially |
| `WYTHOFF` | Deterministic quasi-periodic schedule using Wythoff's construction (golden ratio). Produces uniform coverage and a 1:phi push-to-pull ratio that favors convergence healing. |
| `QUANTUM` | Fully quantum-random peer selection via Hadamard circuit measurements (Julia/Yao.jl). Cannot resonate with periodic network faults. |
| `QUANTUM_HYBRID` | Wythoff base schedule with 20% quantum-random peer swaps and quantum-jittered timing. Preserves Wythoff's coverage guarantees while breaking fault correlations. |

**Quantum Computing** (`src/quantum/`, `src/quantum/julia/`) — Julia embedded in C++ via the Julia C API. Two quantum modules:
- **QuantumRNG**: Generates random numbers by applying Hadamard gates to qubits in the |0> state and measuring. Used for gossip peer selection and timing jitter.
- **QuantumOptimizer**: QAOA (Quantum Approximate Optimization Algorithm) for batch order matching. Encodes bid/ask matching as a QUBO problem, explores the solution space via parameterized quantum circuits, and optimizes parameters with Nelder-Mead. Falls back to classical greedy for >20 candidates.

**Network Layer** (`src/network/`) — TCP server for client commands, UDP transport for gossip messages between nodes.

**API** (`src/api/`) — Text-based TCP protocol. Commands:

| Command | Description |
|---|---|
| `ADD BUY\|SELL <price> <qty>` | Place a limit order |
| `CANCEL <order_id>` | Cancel an order |
| `MARKET BUY\|SELL <qty>` | Execute a market order |
| `BOOK [depth]` | Get order book snapshot |
| `STATUS` | Node status and vector clock |
| `PEERS` | List peer nodes and liveness |
| `METRICS` | Prometheus-format metrics |
| `QMATCH` | Preview QAOA-optimized batch matching |
| `QCOMPARE` | Compare quantum vs classical matching surplus |
| `QSTATUS` | Quantum subsystem status |

**Frontend** (`frontend/`) — React/Vite dashboard with:
- Order book visualization per node (bids/asks with depth)
- Depth chart (cumulative bid/ask volume)
- Price chart (OHLCV candlesticks)
- Order entry form (route to any node)
- Node status panel (liveness, vector clocks, op log size)
- Activity log (command history)
- Market replay engine (plays back historical AAPL intraday + MSFT/NVDA/AMZN/GOOG daily data)

**Bridge Server** (`frontend/server/`) — Node.js WebSocket server that sits between the frontend and C++ nodes. Polls each node via TCP for book/status updates, relays commands from the frontend, and drives the market replay engine.

**Monitoring** (`monitoring/`) — Prometheus scrapes `/METRICS` from each node. Grafana dashboards visualize convergence time, gossip coverage, and Wythoff schedule analysis.

## Quick Start

### Prerequisites

- Docker and Docker Compose
- (Optional) Julia 1.11+ for quantum features in local development

### Run the full system

```bash
docker compose up --build
```

This starts:
- 5 order book nodes (TCP ports 8001-8005, UDP ports 7001-7005)
- WebSocket bridge server on port 3001
- Frontend on http://localhost:5173
- Prometheus on http://localhost:9090
- Grafana on http://localhost:3000 (admin/admin)

### Choose a gossip strategy

```bash
# Wythoff (default)
GOSSIP_STRATEGY=WYTHOFF docker compose up --build

# Random
GOSSIP_STRATEGY=RANDOM docker compose up --build

# Quantum-enhanced (requires Julia in the Docker image)
GOSSIP_STRATEGY=QUANTUM QUANTUM_ENABLED=1 docker compose up --build

# Hybrid: Wythoff + quantum perturbation
GOSSIP_STRATEGY=QUANTUM_HYBRID QUANTUM_ENABLED=1 docker compose up --build
```

### Send orders manually

```bash
# Add a buy order at $150.00 for 100 shares on node-a
echo "ADD BUY 150.00 100" | nc localhost 8001

# Add a sell order at $155.00 for 50 shares on node-c
echo "ADD SELL 155.00 50" | nc localhost 8003

# Execute a market buy for 25 shares on node-a
echo "MARKET BUY 25" | nc localhost 8001

# Check the order book
echo "BOOK 10" | nc localhost 8001

# Check node status and vector clocks
echo "STATUS" | nc localhost 8001

# Check peer liveness
echo "PEERS" | nc localhost 8001
```

### Quantum commands (when QUANTUM_ENABLED=1)

```bash
# Check if quantum subsystem is active
echo "QSTATUS" | nc localhost 8001

# Preview QAOA-optimized batch matching
echo "QMATCH" | nc localhost 8001

# Compare quantum vs classical matching
echo "QCOMPARE" | nc localhost 8001
```

### Run tests

```bash
# In Docker
docker compose run --rm node-a sh -c "cd /app && cmake -B build && cmake --build build && ./build/convergence_tests"

# Locally (requires Julia for quantum tests to fully exercise)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/convergence_tests
```

### Benchmark gossip strategies

```bash
# Runs each strategy N times, injects network partitions, measures convergence
N_RUNS=5 ./scripts/benchmark_convergence.sh
```

This outputs `benchmark_results.csv` and `benchmark_summary.csv` with mean, stddev, p50, and p95 convergence times per strategy.

### Simulate network partitions

```bash
# Partition node-b for 15 seconds
./scripts/inject_partition.sh
```

## Building Locally

```bash
# Without quantum (no Julia dependency)
cmake -B build -DENABLE_QUANTUM=OFF
cmake --build build -j$(nproc)
./build/convergence

# With quantum (requires Julia installed)
export JULIA_DIR=/path/to/julia  # or just have `julia` on PATH
cmake -B build -DENABLE_QUANTUM=ON
cmake --build build -j$(nproc)
QUANTUM_ENABLED=1 GOSSIP_STRATEGY=QUANTUM_HYBRID ./build/convergence
```

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `NODE_ID` | required | Unique node identifier (e.g. `node-a`) |
| `UDP_PORT` | `7000` | UDP port for gossip protocol |
| `TCP_PORT` | `8000` | TCP port for client connections |
| `SEED_PEERS` | none | Comma-separated peer list (`host:port,host:port`) |
| `GOSSIP_INTERVAL_MS` | `100` | Milliseconds between gossip rounds |
| `GOSSIP_STRATEGY` | `RANDOM` | `RANDOM`, `ROUND_ROBIN`, `WYTHOFF`, `QUANTUM`, `QUANTUM_HYBRID` |
| `QUANTUM_ENABLED` | `0` | Set to `1` to initialize the Julia quantum bridge |
| `JULIA_QUANTUM_DIR` | auto-detected | Path to directory containing `init.jl` |

## Project Structure

```
├── CMakeLists.txt                  # Build system (C++20, Julia integration)
├── Dockerfile                      # Multi-stage: build with Julia, slim runtime
├── docker-compose.yml              # 5 nodes + bridge + frontend + monitoring
├── src/
│   ├── main.cpp                    # Entry point
│   ├── orderbook/                  # Limit order book engine
│   │   ├── order.h                 #   Order struct, Fill struct
│   │   ├── price_level.h           #   Price level with FIFO queue
│   │   ├── orderbook.h/.cpp        #   Add, cancel, match, snapshot
│   ├── crdt/                       # CRDT replication layer
│   │   ├── vector_clock.h/.cpp     #   Vector clock (happens-before, merge)
│   │   ├── crdt_orderbook.h/.cpp   #   Operation log, deduplication, anti-entropy
│   ├── gossip/                     # Gossip protocol
│   │   ├── gossip_engine.h/.cpp    #   Gossip loop, peer selection, message handling
│   │   ├── wythoff_scheduler.h/.cpp#   Golden-ratio scheduling
│   │   ├── message.h/.cpp          #   Serialization (push, pull, heartbeat, anti-entropy)
│   │   ├── peer.h/.cpp             #   Peer state and liveness tracking
│   ├── quantum/                    # Quantum computing integration
│   │   ├── julia_bridge.h/.cpp     #   C++ ↔ Julia interop via julia.h
│   │   ├── quantum_scheduler.h/.cpp#   Quantum/hybrid gossip scheduling
│   │   ├── quantum_matcher.h/.cpp  #   QAOA-optimized order matching
│   │   └── julia/                  #   Julia source modules
│   │       ├── init.jl             #     Entry point, installs Yao.jl
│   │       ├── quantum_rng.jl      #     Hadamard-circuit random number generation
│   │       └── quantum_optimizer.jl#     QAOA matching optimization
│   ├── network/                    # Transport layer
│   │   ├── tcp_server.h/.cpp       #   TCP client connections
│   │   ├── udp_transport.h/.cpp    #   UDP send/receive for gossip
│   ├── api/                        # Client-facing API
│   │   ├── client_handler.h/.cpp   #   Command parsing and dispatch
│   └── util/                       # Utilities
│       ├── config.h/.cpp           #   Environment variable parsing
│       ├── logger.h/.cpp           #   Logging
│       └── uuid.h/.cpp             #   UUID generation
├── tests/                          # Test suite
│   ├── test_framework.h            #   Minimal test macros
│   ├── test_orderbook.cpp          #   Order book unit tests
│   ├── test_vector_clock.cpp       #   Vector clock tests
│   ├── test_crdt.cpp               #   CRDT replication tests
│   ├── test_message_serialization.cpp
│   ├── test_wythoff_scheduler.cpp  #   Wythoff coverage and scheduling tests
│   ├── test_quantum.cpp            #   Quantum scheduler, bridge, matcher tests
│   └── test_main.cpp               #   Test runner
├── frontend/                       # React dashboard
│   ├── src/                        #   Components, hooks, styles
│   ├── server/                     #   Node.js WebSocket bridge + replay engine
│   └── data/                       #   Historical market data (AAPL, MSFT, etc.)
├── scripts/                        # Operational scripts
│   ├── benchmark_convergence.sh    #   Strategy comparison benchmark
│   ├── inject_partition.sh         #   Network partition injection
│   ├── send_order.py               #   Order submission helper
│   └── monitor_convergence.py      #   Live convergence monitoring
└── monitoring/                     # Observability
    ├── prometheus.yml              #   Prometheus scrape config
    └── grafana/dashboards/         #   Grafana dashboard JSON
```

## Gossip Scheduling: Wythoff's Construction

Wythoff's construction generates pairs (floor(n*phi), floor(n*phi^2)) where phi = (1+sqrt(5))/2, producing two Beatty sequences that partition the positive integers. This builds a deterministic, quasi-periodic gossip schedule.

The schedule never repeats (unlike round-robin), so it cannot resonate with periodic network faults. Every node pair gets uniform coverage over any window, and the natural 1:phi push-to-pull ratio biases toward pull, which heals inconsistency after partitions.

```cpp
GossipPair WythoffScheduler::get_pair(uint64_t round) const {
    uint32_t num_pairs = num_nodes_ * (num_nodes_ - 1);
    auto wythoff_val = static_cast<uint64_t>(std::floor(round * PHI));
    uint32_t pair_idx = static_cast<uint32_t>(wythoff_val % num_pairs);
    uint32_t sender = pair_idx / (num_nodes_ - 1);
    uint32_t receiver = pair_idx % (num_nodes_ - 1);
    if (receiver >= sender) receiver++;
    return {sender, receiver};
}
```

## Quantum Computing Integration

The quantum layer is implemented in Julia using the Yao.jl framework and embedded into C++ via Julia's C API. It provides two capabilities:

**Quantum RNG** — Random numbers generated by preparing qubits in the |0> state, applying Hadamard gates to create equal superposition, and measuring. Each measurement collapses to 0 or 1 with equal probability, producing fundamentally non-deterministic bits.

**QAOA Order Matching** — The bid/ask matching problem is encoded as a QUBO (Quadratic Unconstrained Binary Optimization): each potential match is a binary variable, the objective maximizes total surplus weighted by quantity, and penalty terms enforce one-match-per-order constraints. A parameterized quantum circuit alternates between problem unitaries (ZZ interactions from the QUBO matrix) and mixer unitaries (X rotations), with parameters optimized by Nelder-Mead.

The system compiles and runs without Julia installed. When `CONVERGENCE_QUANTUM_ENABLED=0`, the bridge methods return safe defaults and the scheduler falls back to Wythoff.
