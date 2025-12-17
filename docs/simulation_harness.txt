# Simulations with Test Harness

The test harness enables containerized cluster simulation with configurable network topology using Linux traffic control (`tc`).

## Overview

- Spawns controller and agent containers in a Docker network
- Applies `tc` rules to simulate inter-rack latency
- Collects results and generates topology visualization

## Harness Commands

```bash
# Start cluster
./build/bin/harness --config cluster.json up

# Check status
./build/bin/harness --config cluster.json status

# Collect results (waits for completion, generates topology SVG)
./build/bin/harness --config cluster.json collect

# Stop and cleanup
./build/bin/harness --config cluster.json down
```

## Cluster Configuration

**Simple cluster (no network shaping):**
```json
{
  "name": "simple-4-node",
  "num_agents": 4,
  "image": "nixl-topo-disc:latest"
}
```

**Two-rack simulation with latency shaping:**
```json
{
  "name": "two-rack",
  "num_agents": 8,
  "image": "nixl-topo-disc:latest",
  "network_shaping": {
    "enabled": true,
    "tiers": [
      {
        "name": "rack-0",
        "agents": [0, 1, 2, 3],
        "intra_tier_latency_us": 100
      },
      {
        "name": "rack-1",
        "agents": [4, 5, 6, 7],
        "intra_tier_latency_us": 100
      }
    ],
    "inter_tier": {
      "latency_us": 5000,
      "jitter_us": 500
    }
  }
}
```

## Network Shaping Parameters

| Parameter | Description |
|-----------|-------------|
| `tiers[].agents` | Agent IDs belonging to this tier (e.g., rack) |
| `tiers[].intra_tier_latency_us` | Latency between agents in same tier (microseconds) |
| `inter_tier.latency_us` | Latency between agents in different tiers |
| `inter_tier.jitter_us` | Latency variance for inter-tier traffic |

## Harness Options

| Option | Description |
|--------|-------------|
| `--config <file>` | Cluster configuration JSON (required) |
| `--output <dir>` | Output directory (default: `./output`) |
| `--test-config <file>` | Test config JSON path inside container |

## Workflow Example

```bash
# 1. Build Docker image
cmake --build build --target docker-build

# 2. Create cluster config
cat > cluster.json << 'EOF'
{
  "name": "test-cluster",
  "num_agents": 4,
  "image": "nixl-topo-disc:latest"
}
EOF

# 3. Start cluster
./build/bin/harness --config cluster.json up

# 4. Wait and collect results
./build/bin/harness --config cluster.json collect

# 5. Results in ./output/<timestamp>/
ls ./output/

# 6. Cleanup
./build/bin/harness --config cluster.json down
```

## Output Files

After `collect`, results are saved to a timestamped subdirectory:
- `latency_matrix.csv` - Pairwise latencies
- `bandwidth_matrix.csv` - Pairwise bandwidth
- `topology.svg` - Inferred topology visualization
