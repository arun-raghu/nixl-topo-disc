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

The harness mounts a host directory into the controller container at `/output`. When tests complete, the `collect` command copies results from the container to a timestamped subdirectory on the host.

**Specifying output directory:**
```bash
# Use custom output directory
./build/bin/harness --config cluster.json --output /path/to/results up
./build/bin/harness --config cluster.json --output /path/to/results collect
```

**Default behavior:**
- Output directory: `./output` (relative to current working directory)
- Results copied to: `<output_dir>/<timestamp>/`

**Files collected:**
- `latency_matrix.csv` - Pairwise latencies (nanoseconds)
- `bandwidth_matrix.csv` - Pairwise bandwidth (MB/s)
- `latency_detailed.csv` - Latency sweep results (if enabled)
- `bandwidth_detailed.csv` - Bandwidth at each message size
- `topology.svg` - Inferred topology visualization

## Topology Visualization with Simulated Clusters

When running `topology_viz` on results from a simulated cluster, the tier threshold configuration should match the latency values used by `tc` to set up the network topology. This ensures the topology inference correctly identifies the simulated rack boundaries.

The harness automatically generates a `tier_config.json` file during result collection, using the geometric mean of intra-tier and inter-tier latencies as the threshold: `threshold = sqrt(intra_tier * inter_tier)`. This ensures proper separation between tiers without manual configuration.

**Example:** If your cluster config uses:
- `intra_tier_latency_us: 100` (100us within rack)
- `inter_tier.latency_us: 5000` (5ms between racks)

Then configure `topology_viz` with matching thresholds:
```bash
# Create tier config matching simulated latencies
cat > tier_config.json << 'EOF'
{
  "tier_thresholds": [500000, 3000000]
}
EOF

# tier_thresholds in nanoseconds:
#   < 500us  (500000ns)  -> Tier 0 (same rack)
#   < 3ms    (3000000ns) -> Tier 1 (different rack, same cluster)

./build/bin/topology_viz latency_matrix.csv -c tier_config.json -o topology.svg
```

This alignment between simulated latencies and inference thresholds allows the topology constructor to correctly group nodes that share the same simulated rack.
