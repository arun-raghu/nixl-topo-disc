# Topology Constructor Module

The topology module infers physical network structure from latency measurements.

## Overview

Uses hierarchical clustering to group nodes by latency similarity, then infers hidden network elements (switches) that explain the observed latencies.

## Input

NxN latency matrix CSV file (nanoseconds), as produced by the controller.

## Running Topology Visualization

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

## Tier Configuration

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

## Output Graph Structure

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

## Interpreting Visualizations

- **Clustered nodes at Tier 0**: Share low-latency interconnect (likely same node)
- **Clustered nodes at Tier 1**: Share a ToR switch (likely same rack)
- **Hidden nodes**: Represent inferred network equipment
- **Confidence scores**: Higher values (closer to 1.0) indicate more reliable inference
- **Edge thickness/color**: Indicates tier level (green=fast, blue=medium, gray=slow)
