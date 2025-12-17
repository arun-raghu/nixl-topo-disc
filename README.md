# Cluster Topology Discovery & Performance Measurement System

A distributed agent system that measures memory transfer performance between cluster nodes using NVIDIA NIXL, and infers physical network topology from latency measurements.

## Features

- **Pairwise Latency Tests**: Measure round-trip latency between all node pairs
- **Bandwidth Tests**: Measure unidirectional throughput with configurable message sizes
- **Latency Sweep**: Generate transfer time vs. message size curves
- **Topology Inference**: Build cluster topology graph from latency matrix using hierarchical clustering

## Prerequisites

**Operating System:**
- Ubuntu 22.04 (or compatible Linux distribution)

**Build Tools:**
```bash
sudo apt update
sudo apt install -y cmake g++ ninja-build pkg-config
```

**Autotools (for NIXL/UCX submodules):**
```bash
sudo apt install -y autoconf automake libtool
```

**Python (for meson build):**
```bash
sudo apt install -y python3 python3-pip
pip3 install meson>=0.64.0
```

**RDMA Development Libraries (optional, for InfiniBand support):**
```bash
sudo apt install -y libibverbs-dev librdmacm-dev libnuma-dev
```

**GraphViz (for topology visualization):**
```bash
sudo apt install -y graphviz
```

## Building from Source

```bash
# Clone with submodules (includes NIXL, UCX)
git clone --recursive <repo-url>
cd nixl-topo-disc

# Configure and build (builds NIXL/UCX from submodules)
cmake -B build -DBUILD_DEPS_FROM_SOURCE=ON
cmake --build build -j$(nproc)

# Source environment (sets LD_LIBRARY_PATH for NIXL/UCX)
source build/bin/env-setup.sh
```

## Running Tests

### Local Process Mode

Run the controller which spawns agent processes locally:

```bash
# Source environment first (required for NIXL/UCX libraries)
source build/bin/env-setup.sh

./build/bin/controller -n 2 -c config.json
```

**Arguments:**
- `-n, --num-agents <N>`: Number of agents to spawn (required)
- `-c, --config <file>`: JSON configuration file (optional)
- `-h, --help`: Show usage

### Docker Mode

Build and run in containers:

```bash
# Build Docker image (from build directory)
cd build
cmake --build . --target docker-build

# Or directly with docker
docker build -t nixl-topo-disc:latest .
```

### Example Configuration (JSON)

```json
{
  "ping_pong": {
    "message_size": 64,
    "iterations": 1000,
    "warmup_iterations": 100
  },
  "bandwidth": {
    "message_sizes": [1024, 4096, 16384, 65536, 262144, 1048576],
    "iterations": 100,
    "warmup_iterations": 10,
    "window_size": 16
  },
  "latency_sweep": {
    "enabled": true,
    "message_sizes": [64, 1024, 4096, 16384, 65536],
    "iterations": 100,
    "warmup_iterations": 10
  },
  "output": {
    "latency_matrix": "/tmp/latency_matrix.csv",
    "bandwidth_matrix": "/tmp/bandwidth_matrix.csv",
    "bandwidth_detailed": "/tmp/bandwidth_detailed.csv",
    "latency_detailed": "/tmp/latency_detailed.csv"
  }
}
```

## Interpreting Results

See [docs/interpreting_results.md](docs/interpreting_results.md) for output file formats and sample data.

## Topology Constructor Module

Infers physical network topology from latency measurements using hierarchical clustering. Groups nodes into tiers based on latency thresholds and identifies hidden network elements (switches) that explain observed communication patterns. See [docs/topology_module.md](docs/topology_module.md) for details.

## Simulation Assumptions

1. **NIXL-only IPC**: All data transfer uses NIXL RDMA primitives; no TCP sockets for test data
2. **Shared Memory Transport**: Local testing uses UCX shared memory (`shm`) transport
3. **Agent Buffer Size**: Each agent allocates 256MB for test buffers
4. **Wire Format**: Big-endian byte order for cross-platform RDMA compatibility
5. **Latency Measurement**: Ping-pong measures round-trip time; symmetric latency assumed
6. **Bandwidth Measurement**: Unidirectional throughput per test; window-based pipelining

## Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `UCX_TLS` | UCX transport selection | `tcp`, `shm`, `rc`, `dc` |
| `UCX_LOG_LEVEL` | UCX diagnostic verbosity | `warn`, `info`, `debug` |
| `NIXL_LOG_LEVEL` | NIXL logging level | `0` (off) to `5` (trace) |

**Transport Selection:**
- `shm`: Shared memory (single-node testing)
- `tcp`: TCP sockets (fallback, no RDMA)
- `rc`: InfiniBand Reliable Connection
- `dc`: InfiniBand Dynamically Connected

## Unit Tests

```bash
cd build
ctest --output-on-failure
```

## Simulations with Test Harness

See [docs/simulation_harness.md](docs/simulation_harness.md) for containerized cluster simulation with network shaping.

## Project Structure

```
nixl-topo-disc/
├── src/
│   ├── agent/          # Agent process implementation
│   ├── controller/     # Controller orchestration
│   ├── topology/       # Topology inference module
│   └── common/         # Shared types, NIXL wrapper
├── tests/              # Unit tests
├── docs/               # Design documentation
├── submodules/
│   ├── nixl/           # NVIDIA NIXL SDK
│   └── ucx/            # UCX communication library
└── CMakeLists.txt
```

## License

See LICENSE file.
