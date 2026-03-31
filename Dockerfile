FROM ubuntu:24.04 AS builder

# Install build tools
RUN apt-get update && apt-get install -y \
    cmake g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY CMakeLists.txt .
COPY src/ src/
COPY tests/ tests/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    iproute2 iptables iputils-ping net-tools \
    && rm -rf /var/lib/apt/lists/*

# Copy application
COPY --from=builder /app/build/convergence /usr/local/bin/convergence

ENTRYPOINT ["convergence"]
