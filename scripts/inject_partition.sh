#!/bin/bash
# Partition a node from the network for N seconds, then heal.
# Usage: ./inject_partition.sh <container_name> <seconds>
# Example: ./inject_partition.sh convergence-node-c-1 10

CONTAINER=$1
DURATION=$2

if [ -z "$CONTAINER" ] || [ -z "$DURATION" ]; then
    echo "Usage: $0 <container_name> <seconds>"
    exit 1
fi

echo "Partitioning $CONTAINER for ${DURATION}s..."
docker exec "$CONTAINER" iptables -A INPUT -p udp --dport 7000 -j DROP
docker exec "$CONTAINER" iptables -A OUTPUT -p udp --dport 7000 -j DROP

sleep "$DURATION"

echo "Healing partition on $CONTAINER..."
docker exec "$CONTAINER" iptables -D INPUT -p udp --dport 7000 -j DROP
docker exec "$CONTAINER" iptables -D OUTPUT -p udp --dport 7000 -j DROP
echo "Done. Watch convergence monitor for re-sync."
