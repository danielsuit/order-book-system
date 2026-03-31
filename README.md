# Convergence

A peer-to-peer replicated order book. Five nodes maintain independent copies of a limit order book and sync state over a UDP gossip protocol using CRDTs. There is no central server. Any node can accept orders. Updates propagate through the network until all nodes converge to the same book.

Built in C++20 with raw TCP/UDP sockets, Docker Compose orchestration, a React frontend dashboard, and Prometheus/Grafana monitoring.

```
     Node A                Node B                Node C
    ┌──────────────┐      ┌──────────────┐      ┌──────────────┐
    │ TCP API :8000 │      │ TCP API :8000 │      │ TCP API :8000 │
    │      │        │      │      │        │      │      │        │
    │ Order Book    │      │ Order Book    │      │ Order Book    │
    │      │        │      │      │        │      │      │        │
    │ CRDT + VClock │◄─UDP─►│ CRDT + VClock │◄─UDP─►│ CRDT + VClock │
    │      │        │      │      │        │      │      │        │
    │ Gossip Engine │      │ Gossip Engine │      │ Gossip Engine │
    └──────────────┘      └──────────────┘      └──────────────┘
                                  ▲
                           UDP    │    UDP
                    ┌─────────────┼─────────────┐
               ┌────▼─────┐                ┌────▼─────┐
               │  Node D   │                │  Node E   │
               └───────────┘                └───────────┘
```

## Quick Start

```bash
docker compose up --build
```

This starts 5 order book nodes, a WebSocket bridge, the React frontend, Prometheus, and Grafana:

| Service | URL |
|---|---|
| Frontend | http://localhost:5173 |
| Nodes (TCP) | localhost:8001–8005 |
| Prometheus | http://localhost:9090 |
| Grafana | http://localhost:3000 (admin/admin) |

Send orders from the command line:

```bash
# Place a buy order at $150.00 for 100 shares on node-a
echo "ADD BUY 150.00 100" | nc localhost 8001

# Place a sell order on node-c — it propagates to all nodes
echo "ADD SELL 155.00 50" | nc localhost 8003

# Execute a market buy
echo "MARKET BUY 25" | nc localhost 8001

# Check the book on any node
echo "BOOK 10" | nc localhost 8005

# Inspect vector clocks and peer liveness
echo "STATUS" | nc localhost 8001
echo "PEERS" | nc localhost 8001
```

## How It Works

### Order Book

Standard limit order book with price-time priority. Bids sorted descending, asks sorted ascending via `std::map`. Each price level holds a FIFO queue of orders. Supports limit order placement, cancellation, and market order execution that walks the book and returns fills.

### CRDT Replication

Each order book is wrapped in a CRDT layer that assigns Lamport timestamps (from a vector clock) to every operation. Operations are `ADD_ORDER` and `CANCEL_ORDER`. When a remote operation arrives, it's applied idempotently — duplicates are rejected via an op-ID set, and conflicts are resolved by timestamp with node ID as tiebreaker.

### Gossip Protocol

Nodes exchange operations over UDP. Each gossip round, a node selects a peer and either pushes its recent operations or pulls the peer's. Three peer-selection strategies are supported:

- **Random** — pick a random alive peer each round.
- **Round-robin** — cycle through peers sequentially.
- **Wythoff** — deterministic quasi-periodic schedule based on the golden ratio (see below).

Heartbeats detect dead peers. When a peer comes back, anti-entropy kicks in: the recovering node requests the full operation delta from its neighbors.

### Network Partition Tolerance

```bash
# Partition node-b from gossip for 15 seconds, then heal
./scripts/inject_partition.sh convergence-node-b-1 15
```

While partitioned, the isolated node misses updates. On heal, anti-entropy triggers automatically and the node catches up. The convergence monitor tracks how long this takes:

```bash
python3 scripts/monitor_convergence.py
```

## Wythoff Scheduling

The default gossip strategy uses Wythoff's construction — a number-theoretic method that generates gossip pairs from the golden ratio.

Each round, the scheduler computes `floor(round × φ)` and maps the result to a (sender, receiver) pair across all nodes. Because φ is irrational, the sequence never repeats — unlike round-robin, it can't resonate with periodic network faults. Over any window, every node pair gets uniform coverage.

Push vs. pull is determined by Beatty sequence membership. The two Beatty sequences of φ and φ² partition the positive integers with a density ratio of 1/φ to 1/φ², which gives a natural pull bias (~61.8% pull, ~38.2% push). This favors convergence healing after partitions.

Nodes also offset their gossip timing by the golden angle (`node_index × interval / φ mod interval`) to prevent synchronized bursts.

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

The test suite verifies the properties that make this work: uniform pair coverage within 25% of expected across 10,000 rounds, the Beatty partition property (every round is exactly one of push or pull), determinism across scheduler instances, no self-gossip, and non-periodicity for all periods ≤ 50.

### Benchmarking Strategies

```bash
N_RUNS=5 ./scripts/benchmark_convergence.sh
```

Runs each strategy N times, injects network partitions, and measures convergence time. Outputs `benchmark_results.csv` and `benchmark_summary.csv` with mean, stddev, p50, and p95 per strategy.

## TCP Protocol

All commands are newline-terminated text over TCP.

| Command | Response |
|---|---|
| `ADD BUY 150.00 100` | `OK <order_id>` |
| `ADD SELL 155.00 50` | `OK <order_id>` |
| `CANCEL <order_id>` | `OK` or `ERROR order_not_found` |
| `MARKET BUY 25` | `FILLED <n>` followed by price/qty lines |
| `BOOK [depth]` | `BIDS` and `ASKS` with price levels |
| `STATUS` | Node ID, order count, op log size, vector clock |
| `PEERS` | Peer list with host, port, liveness |
| `METRICS` | Prometheus-format metrics |

## Project Structure

```
├── CMakeLists.txt                    C++20, -Wall -Wextra -Wpedantic
├── Dockerfile                        Multi-stage build (Ubuntu 24.04)
├── docker-compose.yml                5 nodes + bridge + frontend + monitoring
│
├── src/
│   ├── main.cpp                      Entry point, signal handling, startup
│   ├── orderbook/
│   │   ├── order.h                   Order and Fill structs
│   │   ├── price_level.h             Price level with FIFO deque
│   │   └── orderbook.h/.cpp          Add, cancel, match, snapshot, replication
│   ├── crdt/
│   │   ├── vector_clock.h/.cpp       Vector clock with binary serialization
│   │   └── crdt_orderbook.h/.cpp     Operation log, dedup, anti-entropy
│   ├── gossip/
│   │   ├── gossip_engine.h/.cpp      Gossip loop, peer selection, message dispatch
│   │   ├── wythoff_scheduler.h/.cpp  Golden-ratio pair scheduling
│   │   ├── message.h/.cpp            Binary serialization (network byte order)
│   │   └── peer.h/.cpp               Peer state and liveness
│   ├── network/
│   │   ├── tcp_server.h/.cpp         Client-facing TCP server
│   │   └── udp_transport.h/.cpp      UDP send/recv for gossip
│   ├── api/
│   │   └── client_handler.h/.cpp     Command parsing, Prometheus metrics
│   └── util/
│       ├── config.h/.cpp             Environment variable config
│       ├── logger.h/.cpp             Timestamped logging
│       └── uuid.h/.cpp               UUID generation
│
├── tests/
│   ├── test_orderbook.cpp            Order book unit tests (12 tests)
│   ├── test_vector_clock.cpp         Vector clock ordering and merge
│   ├── test_crdt.cpp                 CRDT replication and convergence
│   ├── test_message_serialization.cpp Binary round-trip tests
│   ├── test_wythoff_scheduler.cpp    Coverage, Beatty, determinism, periodicity
│   ├── test_framework.h              Minimal test macros
│   └── test_main.cpp                 Test runner
│
├── frontend/
│   ├── src/                          React components (OrderBook, DepthChart,
│   │                                 PriceChart, OrderEntry, NodeStatus, ActivityLog)
│   ├── server/                       Node.js WebSocket bridge + replay engine
│   └── data/                         Historical OHLCV data (AAPL, MSFT, NVDA, AMZN, GOOG)
│
├── scripts/
│   ├── benchmark_convergence.sh      Strategy comparison with stats
│   ├── inject_partition.sh           iptables-based network partition
│   ├── monitor_convergence.py        Live convergence tracker
│   └── send_order.py                 TCP order submission helper
│
└── monitoring/
    ├── prometheus.yml                Scrape config for all nodes
    └── grafana/dashboards/           Convergence and gossip dashboards
```

## Building

### With Docker (recommended)

```bash
docker compose up --build
```

### Locally

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run a single node
NODE_ID=node-a UDP_PORT=7000 TCP_PORT=8000 ./build/convergence

# Run tests
./build/convergence_tests
```

### Environment Variables

| Variable | Default | Description |
|---|---|---|
| `NODE_ID` | required | Unique node identifier |
| `UDP_PORT` | `7000` | Gossip protocol port |
| `TCP_PORT` | `8000` | Client API port |
| `SEED_PEERS` | — | Comma-separated `host:port` list |
| `GOSSIP_INTERVAL_MS` | `100` | Gossip round interval |
| `GOSSIP_STRATEGY` | `RANDOM` | `RANDOM`, `ROUND_ROBIN`, or `WYTHOFF` |

## Tests

```bash
./build/convergence_tests
```

Covers: order book operations (add, cancel, match, FIFO priority, snapshots, replication), vector clock causality and merge, CRDT convergence across two simulated nodes, binary message round-trip serialization, and Wythoff scheduler properties (coverage uniformity, Beatty partition, determinism, non-periodicity, phase offset spread).