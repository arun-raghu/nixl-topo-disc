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
./build/bin/controller -n 2 -c config.json
```

**Arguments:**
- `-n, --num-agents <N>`: Number of agents to spawn (required)
- `-c, --config <file>`: JSON configuration file (optional)
- `-h, --help`: Show usage

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

### Output Files

| File | Description | Format |
|------|-------------|--------|
| `/tmp/latency_matrix.csv` | NxN round-trip latency matrix | `node_i,node_j,latency_ns` |
| `/tmp/bandwidth_matrix.csv` | NxN peak bandwidth matrix | `node_i,node_j,bandwidth_mbps` |
| `/tmp/bandwidth_detailed.csv` | Bandwidth at each message size | `initiator,responder,msg_size,bandwidth_mbps` |
| `/tmp/latency_detailed.csv` | Latency at each message size | `initiator,responder,msg_size,avg_latency_ns,min_latency_ns,max_latency_ns` |

### Sample Output

**Latency Matrix:**
```
0,1,5074
1,0,5089
```
Each row shows: source node, destination node, RTT in nanoseconds.

**Bandwidth Detailed:**
```
initiator,responder,msg_size,bandwidth_mbps
0,1,65536,8521.3
0,1,1048576,12847.2
```

## Topology Constructor Module

The topology module infers physical network structure from latency measurements.

### Overview

Uses hierarchical clustering to group nodes by latency similarity, then infers hidden network elements (switches) that explain the observed latencies.

### Input

NxN latency matrix CSV file (nanoseconds), as produced by the controller.

### Running Topology Visualization

```bash
# Generate PNG visualization (requires GraphViz)
./build/bin/topology_viz /tmp/latency_matrix.csv -o topology.png

# Generate SVG
./build/bin/topology_viz /tmp/latency_matrix.csv -o topology.svg

# Generate DOT file only (no GraphViz required)
./build/bin/topology_viz /tmp/latency_matrix.csv -o topology.dot

# Print adjacency list to stdout
./build/bin/topology_viz /tmp/latency_matrix.csv -a

# Print JSON representation
./build/bin/topology_viz /tmp/latency_matrix.csv -j

# Use custom tier thresholds
./build/bin/topology_viz /tmp/latency_matrix.csv -c tier_config.json
```

### Tier Configuration

Latency thresholds determine how nodes are grouped into tiers:

| Tier | Default Threshold | Interpretation |
|------|-------------------|----------------|
| 0 | < 5 us | Intra-node (NVLink, PCIe) |
| 1 | < 15 us | Intra-rack (NVSwitch, ToR switch) |
| 2 | < 50 us | Inter-rack (spine switch) |
| 3+ | >= 50 us | Inter-pod / WAN |

**Custom configuration (JSON):**
```json
{
  "tier_thresholds": [5000, 15000, 50000],
  "linkage": "single",
  "min_cluster_size_for_switch": 2,
  "min_confidence_threshold": 0.5
}
```

### Output Graph Structure

The topology graph contains:

- **Physical Nodes**: Agents/GPUs in the cluster (solid boxes in visualization)
- **Hidden Nodes**: Inferred switches and shared links (dashed diamonds)
- **Edges**: Connections with latency labels

**Hidden Node Types:**
| Type | Description |
|------|-------------|
| NVSWITCH | Inferred NVSwitch (intra-node GPU interconnect) |
| TOR_SWITCH | Top-of-Rack switch |
| SPINE_SWITCH | Spine/aggregation switch |
| SHARED_LINK | Shared bandwidth bottleneck |

### Interpreting Visualizations

- **Clustered nodes at Tier 0**: Share low-latency interconnect (likely same node)
- **Clustered nodes at Tier 1**: Share a ToR switch (likely same rack)
- **Hidden nodes**: Represent inferred network equipment
- **Confidence scores**: Higher values (closer to 1.0) indicate more reliable inference
- **Edge thickness/color**: Indicates tier level (green=fast, blue=medium, gray=slow)

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
