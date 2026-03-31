#!/bin/bash
# ===========================================================================
# Convergence benchmark: compare RANDOM, ROUND_ROBIN, WYTHOFF gossip strategies
#
# For each strategy:
#   1. Start 5-node cluster with that strategy
#   2. Wait for cluster to stabilize
#   3. Send 100 orders to node-a
#   4. Inject network partition on node-b for 15 seconds
#   5. Send 50 more orders to node-c during partition
#   6. Heal partition
#   7. Measure time until all 5 nodes have identical book state
#   8. Record: strategy, convergence_time_ms, max_divergence_during_partition
#   9. Repeat N_RUNS times per strategy
#   10. Output CSV with statistics
# ===========================================================================

set -euo pipefail

STRATEGIES=("RANDOM" "ROUND_ROBIN" "WYTHOFF")
N_RUNS=${N_RUNS:-5}
PARTITION_DURATION=${PARTITION_DURATION:-15}
RESULTS_FILE="benchmark_results.csv"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

cd "$PROJECT_DIR"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
send_order() {
    local port=$1
    local cmd=$2
    echo "$cmd" | nc -w 2 localhost "$port" 2>/dev/null || true
}

get_book() {
    local port=$1
    echo "BOOK 20" | nc -w 2 localhost "$port" 2>/dev/null || echo ""
}

all_books_match() {
    local book_a book_b book_c book_d book_e
    book_a=$(get_book 8001)
    book_b=$(get_book 8002)
    book_c=$(get_book 8003)
    book_d=$(get_book 8004)
    book_e=$(get_book 8005)

    # All must be non-empty and identical
    if [ -z "$book_a" ]; then return 1; fi
    if [ "$book_a" != "$book_b" ]; then return 1; fi
    if [ "$book_a" != "$book_c" ]; then return 1; fi
    if [ "$book_a" != "$book_d" ]; then return 1; fi
    if [ "$book_a" != "$book_e" ]; then return 1; fi
    return 0
}

count_distinct_states() {
    local states=()
    for port in 8001 8002 8003 8004 8005; do
        local book
        book=$(get_book "$port")
        if [ -n "$book" ]; then
            states+=("$(echo "$book" | md5sum | cut -d' ' -f1)")
        fi
    done
    echo "${states[@]}" | tr ' ' '\n' | sort -u | wc -l | tr -d ' '
}

wait_for_cluster() {
    echo "  Waiting for cluster to start..."
    for i in $(seq 1 30); do
        if echo "STATUS" | nc -w 1 localhost 8001 2>/dev/null | grep -q "NODE"; then
            echo "  Cluster ready after ${i}s"
            return 0
        fi
        sleep 1
    done
    echo "  ERROR: Cluster did not start in 30s"
    return 1
}

inject_partition() {
    local container=$1
    local duration=$2
    echo "  Injecting partition on $container for ${duration}s..."
    docker exec "$container" iptables -A INPUT -p udp --dport 7000 -j DROP 2>/dev/null || true
    docker exec "$container" iptables -A OUTPUT -p udp --dport 7000 -j DROP 2>/dev/null || true
    sleep "$duration"
    echo "  Healing partition on $container"
    docker exec "$container" iptables -F 2>/dev/null || true
}

measure_convergence_ms() {
    local start_ms
    start_ms=$(date +%s%3N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1000))')
    local timeout=30000  # 30 seconds max

    while true; do
        local now_ms
        now_ms=$(date +%s%3N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1000))')
        local elapsed=$((now_ms - start_ms))

        if all_books_match; then
            echo "$elapsed"
            return 0
        fi

        if [ "$elapsed" -gt "$timeout" ]; then
            echo "$timeout"
            return 1
        fi

        sleep 0.2
    done
}

# ---------------------------------------------------------------------------
# Main benchmark loop
# ---------------------------------------------------------------------------
echo "strategy,run,convergence_ms,max_divergence" > "$RESULTS_FILE"

# Find the node-b container name
get_node_b_container() {
    docker compose ps --format '{{.Name}}' 2>/dev/null | grep node-b | head -1
}

for strategy in "${STRATEGIES[@]}"; do
    echo ""
    echo "============================================"
    echo "  Strategy: $strategy"
    echo "============================================"

    for run in $(seq 1 "$N_RUNS"); do
        echo ""
        echo "--- Run $run / $N_RUNS ---"

        # Start cluster with this strategy
        GOSSIP_STRATEGY="$strategy" docker compose up -d --build --quiet-pull 2>/dev/null
        wait_for_cluster || { docker compose down 2>/dev/null; continue; }

        # Let cluster stabilize
        sleep 3

        # Phase 1: Send 100 orders to node-a
        echo "  Sending 100 orders to node-a..."
        for i in $(seq 1 50); do
            price=$(echo "150.00 + $i * 0.10" | bc)
            send_order 8001 "ADD BUY $price $((i * 10))"
        done
        for i in $(seq 1 50); do
            price=$(echo "160.00 + $i * 0.10" | bc)
            send_order 8001 "ADD SELL $price $((i * 10))"
        done

        # Let orders propagate
        sleep 3

        # Phase 2: Inject partition on node-b
        NODE_B_CONTAINER=$(get_node_b_container)
        if [ -z "$NODE_B_CONTAINER" ]; then
            echo "  ERROR: Could not find node-b container"
            docker compose down 2>/dev/null
            continue
        fi

        inject_partition "$NODE_B_CONTAINER" "$PARTITION_DURATION" &
        PARTITION_PID=$!

        # Phase 3: During partition, send 50 more orders to node-c
        sleep 2
        echo "  Sending 50 orders to node-c during partition..."
        for i in $(seq 1 25); do
            price=$(echo "155.00 + $i * 0.05" | bc)
            send_order 8003 "ADD BUY $price $((i * 5))"
        done
        for i in $(seq 1 25); do
            price=$(echo "165.00 + $i * 0.05" | bc)
            send_order 8003 "ADD SELL $price $((i * 5))"
        done

        # Measure max divergence during partition
        max_divergence=$(count_distinct_states)
        echo "  Max divergence during partition: $max_divergence distinct states"

        # Wait for partition to heal
        wait $PARTITION_PID 2>/dev/null || true

        # Phase 4: Measure convergence time
        echo "  Measuring convergence time..."
        convergence_ms=$(measure_convergence_ms)
        echo "  Convergence time: ${convergence_ms}ms"

        # Record result
        echo "$strategy,$run,$convergence_ms,$max_divergence" >> "$RESULTS_FILE"

        # Tear down
        docker compose down 2>/dev/null
        sleep 2
    done
done

# ---------------------------------------------------------------------------
# Compute statistics
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo "  Results Summary"
echo "============================================"
echo ""

echo "strategy,mean_ms,stddev_ms,p50_ms,p95_ms" > "benchmark_summary.csv"

for strategy in "${STRATEGIES[@]}"; do
    values=$(grep "^$strategy," "$RESULTS_FILE" | cut -d',' -f3 | sort -n)
    if [ -z "$values" ]; then
        echo "$strategy: no results"
        continue
    fi

    count=$(echo "$values" | wc -l | tr -d ' ')
    sum=$(echo "$values" | paste -sd+ | bc)
    mean=$((sum / count))

    # Stddev
    sumsq=0
    for v in $values; do
        diff=$((v - mean))
        sumsq=$((sumsq + diff * diff))
    done
    variance=$((sumsq / count))
    stddev=$(echo "sqrt($variance)" | bc)

    # Percentiles
    p50=$(echo "$values" | sed -n "$((count / 2 + 1))p")
    p95_idx=$(echo "$count * 95 / 100 + 1" | bc)
    [ "$p95_idx" -gt "$count" ] && p95_idx=$count
    p95=$(echo "$values" | sed -n "${p95_idx}p")

    echo "$strategy: mean=${mean}ms stddev=${stddev}ms p50=${p50}ms p95=${p95}ms (n=$count)"
    echo "$strategy,$mean,$stddev,$p50,$p95" >> "benchmark_summary.csv"
done

echo ""
echo "Raw results:  $RESULTS_FILE"
echo "Summary:      benchmark_summary.csv"
