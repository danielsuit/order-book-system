# Convergence — Distributed Order Book

A CRDT-based distributed order book system with gossip replication across 5 Docker nodes, a React frontend dashboard, and Prometheus/Grafana monitoring.

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

### Run tests

```bash
# In Docker
docker compose run --rm node-a sh -c "cd /app && cmake -B build && cmake --build build && ./build/convergence_tests"

# Locally
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
cmake -B build
cmake --build build -j$(nproc)
./build/convergence
```

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `NODE_ID` | required | Unique node identifier (e.g. `node-a`) |
| `UDP_PORT` | `7000` | UDP port for gossip protocol |
| `TCP_PORT` | `8000` | TCP port for client connections |
| `SEED_PEERS` | none | Comma-separated peer list (`host:port,host:port`) |
| `GOSSIP_INTERVAL_MS` | `100` | Milliseconds between gossip rounds |
| `GOSSIP_STRATEGY` | `RANDOM` | `RANDOM`, `ROUND_ROBIN`, `WYTHOFF` |

## Project Structure

```
├── CMakeLists.txt                  # Build system (C++20)
├── Dockerfile                      # Multi-stage build
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

