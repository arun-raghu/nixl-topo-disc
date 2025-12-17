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

Latency thresholds determine how nodes are grouped into tiers. Both the threshold values and number of tiers are fully configurable via JSON.

| Tier | Default Threshold | Interpretation |
|------|-------------------|----------------|
| 0 | < 5 us | Intra-node (NVLink, PCIe) |
| 1 | < 15 us | Intra-rack (ToR switch) |
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

The `tier_thresholds` array can have any number of entries to define custom tier boundaries.

## Output Graph Structure

**Example: 4-node two-rack cluster**

Input latency matrix (nanoseconds):
```
       Node0  Node1  Node2  Node3
Node0      0   3000   8000   8500
Node1   3000      0   8200   8100
Node2   8000   8200      0   2800
Node3   8500   8100   2800      0
```

Output topology graph:
```
                 [Spine-0]
                /         \
               /           \
          [ToR-0]        [ToR-1]
          /    \          /    \
      [Node0] [Node1] [Node2] [Node3]
        |________|      |________|
         ~3000ns          ~2800ns
              |__________|
                 ~8000ns
```

- **Physical Nodes** (boxes): Node0-3 are the measured agents
- **Hidden Nodes** (inferred): ToR-0, ToR-1 (rack switches), Spine-0 (aggregation)
- **Edges**: Labeled with latency; nodes with <5us grouped under same switch

## Interpreting Visualizations

- **Clustered nodes at Tier 0**: Share low-latency interconnect (likely same node)
- **Clustered nodes at Tier 1**: Share a ToR switch (likely same rack)
- **Hidden nodes**: Represent inferred network equipment
- **Confidence scores**: Higher values (closer to 1.0) indicate more reliable inference
- **Edge thickness/color**: Indicates tier level (green=fast, blue=medium, gray=slow)
