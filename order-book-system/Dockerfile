FROM ubuntu:24.04 AS builder

# Install build tools and Julia
RUN apt-get update && apt-get install -y \
    cmake g++ make wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Julia (for quantum computing via Yao.jl)
ARG JULIA_VERSION=1.11.2
RUN wget -q https://julialang-s3.julialang.org/bin/linux/x64/1.11/julia-${JULIA_VERSION}-linux-x86_64.tar.gz \
    && tar -xzf julia-${JULIA_VERSION}-linux-x86_64.tar.gz -C /opt \
    && ln -s /opt/julia-${JULIA_VERSION} /opt/julia \
    && rm julia-${JULIA_VERSION}-linux-x86_64.tar.gz

ENV JULIA_DIR=/opt/julia
ENV PATH="${JULIA_DIR}/bin:${PATH}"

# Pre-install Yao.jl to avoid runtime compilation
RUN julia -e 'import Pkg; Pkg.add("Yao"); using Yao; println("Yao.jl installed")'

WORKDIR /app
COPY CMakeLists.txt .
COPY src/ src/
COPY tests/ tests/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
        -DJULIA_DIR=/opt/julia \
        -DENABLE_QUANTUM=ON \
    && cmake --build build -j$(nproc)

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    iproute2 iptables iputils-ping net-tools \
    && rm -rf /var/lib/apt/lists/*

# Install Julia runtime (needed for quantum features at runtime)
ARG JULIA_VERSION=1.11.2
COPY --from=builder /opt/julia-${JULIA_VERSION} /opt/julia
ENV JULIA_DIR=/opt/julia
ENV PATH="${JULIA_DIR}/bin:${PATH}"

# Copy pre-compiled Julia packages
COPY --from=builder /root/.julia /root/.julia

# Copy application
COPY --from=builder /app/build/convergence /usr/local/bin/convergence

# Copy Julia quantum scripts
COPY src/quantum/julia/ /app/quantum/julia/

ENV JULIA_QUANTUM_DIR=/app/quantum/julia

ENTRYPOINT ["convergence"]
