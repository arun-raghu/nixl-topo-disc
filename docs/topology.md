
---

## 3. Topology Discovery

**Goal:** Infer approximate physical interconnect topology from collected performance data.

### 3.a Graph Construction

#### Graph Model: G(V, HV, E)

```cpp
struct TopologyGraph {
    // V: Physical nodes (agents in the cluster)
    vector<PhysicalNode> V;

    // HV: Virtual/hidden nodes (inferred switches, shared links)
    vector<HiddenNode> HV;

    // E: Edges connecting V and HV nodes
    vector<Edge> E;
};

struct PhysicalNode {
    uint32_t agent_id;
    string hostname;
    NodeType type;              // GPU, CPU, NIC
    uint32_t tier;              // Assigned topology tier (0 = closest)
};

struct HiddenNode {
    uint32_t hv_id;             // Unique ID for hidden node
    HiddenNodeType type;        // SWITCH, SHARED_LINK, BOTTLENECK
    uint32_t tier;              // Topology tier level
    string inferred_label;      // e.g., "NVSwitch-0", "ToR-Switch-1"
    double confidence;          // 0.0-1.0, inference confidence
};

enum class HiddenNodeType {
    NVSWITCH,                   // Inferred NVSwitch (intra-node GPU interconnect)
    TOR_SWITCH,                 // Top-of-Rack switch
    SPINE_SWITCH,               // Spine/aggregation switch
    SHARED_LINK,                // Shared bandwidth bottleneck
    UNKNOWN_BOTTLENECK          // Detected but unclassified
};

struct Edge {
    uint32_t src_id;            // Node ID (physical or hidden)
    uint32_t dst_id;
    bool src_is_hidden;
    bool dst_is_hidden;

    // Edge properties
    double bandwidth_gbps;      // Measured or inferred capacity
    uint64_t latency_ns;        // One-way latency
    double utilization;         // Observed utilization (0.0-1.0)
    EdgeType type;              // DIRECT, SWITCHED, SHARED
};
```

---

#### Incremental Topology Discovery

The algorithm supports **incremental refinement** - produce a useful topology graph quickly with minimal data, then enrich as more measurements become available.

| Level | Test Required | Graph Output | Use Case |
|-------|---------------|--------------|----------|
| **Level 1: Quick** | `PAIRWISE_LATENCY` only | Topology structure, tiers, inferred switches, **default** bandwidth estimates | Fast cluster overview (~minutes) |
| **Level 2: Measured** | + `BANDWIDTH` | Above + **measured** edge capacities | Accurate capacity planning |
| **Level 3: Full** | + `CONCURRENT_BANDWIDTH` | Above + shared bottleneck detection | Complete topology with contention analysis |

**Time Estimates (N agents):**
- Level 1: O(N²) pairs × 11K iterations × ~1µs = ~minutes for 64 nodes
- Level 2: O(N²) pairs × multiple sizes × 100 iterations = ~10-30 minutes
- Level 3: O(N²) pair combinations × 50 iterations = ~hours

**Recommendation:** Run Level 1 first for quick topology overview. Run Level 2/3 overnight or as needed.

---

#### Algorithm Overview

The graph construction proceeds in four phases. **Only Phase 1-2 require data; Phase 3-4 use optional data for refinement.**

```
┌─────────────────────────────────────────────────────────────────┐
│  Input: Latency Matrix L[N][N] (required)                       │
│         Bandwidth Matrix B[N][N] (optional, for Level 2+)       │
│         ConcurrentTestResults (optional, for Level 3)           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 1: Hierarchical Clustering                               │
│  - Cluster nodes by latency similarity                          │
│  - Identify topology tiers (intra-node, intra-rack, inter-rack) │
│  Output: Tier assignments, cluster membership                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 2: Hidden Node Inference                                 │
│  - Infer switch nodes as cluster ancestors                      │
│  - Use latency triangulation to detect intermediaries           │
│  Output: HV nodes with tier assignments                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 3: Edge Construction & Capacity Estimation               │
│  - Connect V nodes to HV nodes                                  │
│  - Estimate edge bandwidth from measurements                    │
│  Output: Edge set E with bandwidth labels                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 4: Shared Bottleneck Detection (Optional)                │
│  - Concurrent transfer correlation analysis                     │
│  - Refine HV nodes for shared links                             │
│  Output: Updated graph with bottleneck annotations              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Output: TopologyGraph G(V, HV, E)                              │
└─────────────────────────────────────────────────────────────────┘
```

---

#### Phase 1: Hierarchical Clustering

**Input:** Latency matrix `L[N][N]` from `PAIRWISE_LATENCY` test

**Algorithm:** Agglomerative hierarchical clustering with single-linkage

```cpp
// Latency-based distance metric
double distance(uint32_t i, uint32_t j, const LatencyMatrix& L) {
    // Use average of both directions for symmetry
    return (L[i][j].avg_ns + L[j][i].avg_ns) / 2.0;
}

struct Dendrogram {
    vector<ClusterNode> nodes;
    // nodes[0..N-1] are leaves (physical nodes)
    // nodes[N..] are internal nodes (merges)
};

struct ClusterNode {
    uint32_t id;
    uint32_t left_child;        // -1 if leaf
    uint32_t right_child;       // -1 if leaf
    double merge_distance;      // Latency at which clusters merged
    vector<uint32_t> members;   // All leaf nodes in this cluster
};

Dendrogram build_dendrogram(const LatencyMatrix& L) {
    // Standard agglomerative clustering
    // 1. Initialize each node as its own cluster
    // 2. Find pair of clusters with minimum distance
    // 3. Merge into new cluster, record merge distance
    // 4. Repeat until single cluster remains
}
```

**Tier Assignment:** Cut dendrogram at configurable thresholds

```cpp
struct TierConfig {
    // Latency thresholds (nanoseconds) - configurable per deployment
    uint64_t tier0_max_ns = 5000;      // < 5µs: intra-node (NVLink/PCIe)
    uint64_t tier1_max_ns = 15000;     // < 15µs: intra-rack (NVSwitch/ToR)
    uint64_t tier2_max_ns = 50000;     // < 50µs: inter-rack (spine)
    // > 50µs: inter-pod / WAN
};

vector<vector<uint32_t>> assign_tiers(
    const Dendrogram& D,
    const TierConfig& config
) {
    vector<vector<uint32_t>> tiers;  // tiers[t] = list of cluster IDs at tier t

    // Walk dendrogram, cut at each threshold
    // Nodes merged below threshold T are in same tier-T cluster
}
```

**Output:**
- `tier_assignment[node_id]` → tier number (0, 1, 2, ...)
- `cluster_membership[node_id]` → cluster ID within tier
- `dendrogram` → full hierarchical structure

---

#### Phase 2: Hidden Node Inference

**Goal:** Infer switch/interconnect nodes that explain the clustering structure

**Principle:** If nodes {A, B, C} form a cluster with similar low latency, they likely connect through a common switch (hidden node).

```cpp
vector<HiddenNode> infer_hidden_nodes(
    const Dendrogram& D,
    const vector<vector<uint32_t>>& tier_clusters,
    const TierConfig& config
) {
    vector<HiddenNode> HV;
    uint32_t hv_id = 0;

    // For each non-trivial cluster, infer a hidden switch node
    for (uint32_t tier = 0; tier < tier_clusters.size(); tier++) {
        for (const auto& cluster : tier_clusters[tier]) {
            if (cluster.members.size() > 1) {
                HiddenNode hv;
                hv.hv_id = hv_id++;
                hv.tier = tier;
                hv.type = classify_hidden_node(tier, cluster.members.size());
                hv.inferred_label = generate_label(hv.type, tier, hv.hv_id);
                hv.confidence = compute_confidence(cluster, D);
                HV.push_back(hv);
            }
        }
    }
    return HV;
}

HiddenNodeType classify_hidden_node(uint32_t tier, size_t cluster_size) {
    switch (tier) {
        case 0:
            // Tier 0 with multiple GPUs → likely NVSwitch
            return cluster_size <= 8 ? HiddenNodeType::NVSWITCH
                                      : HiddenNodeType::TOR_SWITCH;
        case 1:
            return HiddenNodeType::TOR_SWITCH;
        case 2:
            return HiddenNodeType::SPINE_SWITCH;
        default:
            return HiddenNodeType::UNKNOWN_BOTTLENECK;
    }
}
```

**Latency Triangulation (refinement):**

Detect if node B lies on path between A and C:

```cpp
bool is_on_path(uint32_t A, uint32_t B, uint32_t C, const LatencyMatrix& L) {
    double lat_AC = L[A][C].avg_ns;
    double lat_AB = L[A][B].avg_ns;
    double lat_BC = L[B][C].avg_ns;

    // If latency(A,C) ≈ latency(A,B) + latency(B,C), B is on the path
    double tolerance = 0.15;  // 15% tolerance
    double expected = lat_AB + lat_BC;
    return abs(lat_AC - expected) / expected < tolerance;
}
```

This helps refine the tree structure when simple clustering is ambiguous.

---

#### Phase 3: Edge Construction & Capacity Estimation

**Supports incremental discovery:**
- **Level 1 (no bandwidth data):** Uses tier-based default capacities
- **Level 2+ (with bandwidth data):** Uses measured capacities

**Connect physical nodes to hidden nodes:**

```cpp
vector<Edge> construct_edges(
    const vector<PhysicalNode>& V,
    const vector<HiddenNode>& HV,
    const LatencyMatrix& L,
    const BandwidthMatrix& B  // From Phase 2 tests, if available
) {
    vector<Edge> E;

    // Connect each physical node to its cluster's hidden node
    for (const auto& v : V) {
        uint32_t hv_id = get_cluster_hidden_node(v.agent_id);
        if (hv_id != INVALID) {
            Edge e;
            e.src_id = v.agent_id;
            e.dst_id = hv_id;
            e.src_is_hidden = false;
            e.dst_is_hidden = true;

            // Estimate edge latency: half of intra-cluster latency
            e.latency_ns = estimate_edge_latency(v.agent_id, hv_id, L);

            // Bandwidth: use max observed to any peer in same cluster
            e.bandwidth_gbps = estimate_edge_bandwidth(v.agent_id, B);

            e.type = EdgeType::SWITCHED;
            E.push_back(e);
        }
    }

    // Connect hidden nodes hierarchically (switch to spine, etc.)
    for (size_t i = 0; i < HV.size(); i++) {
        uint32_t parent_hv = find_parent_hidden_node(HV[i]);
        if (parent_hv != INVALID) {
            Edge e;
            e.src_id = HV[i].hv_id;
            e.dst_id = parent_hv;
            e.src_is_hidden = true;
            e.dst_is_hidden = true;
            e.latency_ns = estimate_inter_tier_latency(HV[i].tier);
            e.bandwidth_gbps = estimate_aggregated_bandwidth(HV[i], B);
            e.type = EdgeType::SWITCHED;
            E.push_back(e);
        }
    }

    return E;
}
```

**Bandwidth Estimation:**

```cpp
double estimate_edge_bandwidth(uint32_t node_id, const BandwidthMatrix& B) {
    if (B.empty()) {
        // No bandwidth data, use tier-based defaults
        return get_default_bandwidth_for_tier(get_tier(node_id));
    }

    // Use maximum observed bandwidth to any peer
    // (Represents link capacity, not shared capacity)
    double max_bw = 0;
    for (uint32_t peer = 0; peer < B.size(); peer++) {
        if (peer != node_id && B[node_id][peer].bandwidth_gbps > max_bw) {
            max_bw = B[node_id][peer].bandwidth_gbps;
        }
    }
    return max_bw;
}

// Default bandwidths by tier (configurable)
double get_default_bandwidth_for_tier(uint32_t tier) {
    switch (tier) {
        case 0: return 300.0;   // NVLink: ~300-900 GB/s
        case 1: return 100.0;   // NVSwitch/ToR: ~100-400 GB/s
        case 2: return 25.0;    // Spine: ~25-100 GB/s
        default: return 10.0;
    }
}
```

---

#### Phase 4: Shared Bottleneck Detection (Level 3 Only)

**Goal:** Identify hidden shared links by observing bandwidth degradation under concurrent load.

**Only runs for Level 3 topology discovery.** Skipped if `ConcurrentTestResults` not available.

**Requires:** `CONCURRENT_BANDWIDTH` test from section 1.c, which produces `ConcurrentTestResults`.

```cpp
// Uses ConcurrentTestResult from section 1.c Memory Performance Tests

// Detect if two pairs share a bottleneck
bool shares_bottleneck(const ConcurrentTestResult& r) {
    // Significant degradation when concurrent → shared link
    double degradation1 = (r.pair1_solo_bw - r.pair1_concurrent_bw) / r.pair1_solo_bw;
    double degradation2 = (r.pair2_solo_bw - r.pair2_concurrent_bw) / r.pair2_solo_bw;

    const double threshold = 0.20;  // 20% degradation
    return degradation1 > threshold && degradation2 > threshold;
}

// Infer shared link capacity
double infer_shared_capacity(const ConcurrentTestResult& r) {
    // Total bandwidth when concurrent ≈ shared link capacity
    return r.pair1_concurrent_bw + r.pair2_concurrent_bw;
}
```

**Algorithm:**

```cpp
void detect_shared_bottlenecks(
    TopologyGraph& G,
    const vector<ConcurrentTestResult>& concurrent_results
) {
    // Build correlation graph: edge if pairs share bottleneck
    // Find connected components → each component shares a link

    vector<pair<PairID, PairID>> sharing_pairs;
    for (const auto& r : concurrent_results) {
        if (shares_bottleneck(r)) {
            sharing_pairs.push_back({r.pair1, r.pair2});
        }
    }

    // Union-find to group pairs sharing bottlenecks
    auto components = find_connected_components(sharing_pairs);

    // For each component, create SHARED_LINK hidden node
    for (const auto& component : components) {
        HiddenNode hv;
        hv.hv_id = G.HV.size();
        hv.type = HiddenNodeType::SHARED_LINK;
        hv.inferred_label = fmt::format("SharedLink-{}", hv.hv_id);

        // Estimate shared capacity
        hv.capacity_gbps = estimate_shared_capacity(component, concurrent_results);

        G.HV.push_back(hv);

        // Re-route affected edges through this bottleneck node
        update_edges_for_shared_link(G, component, hv.hv_id);
    }
}
```

---

#### Output Data Structures

```cpp
struct TopologyGraphOutput {
    TopologyGraph graph;

    // Summary statistics
    uint32_t num_physical_nodes;
    uint32_t num_hidden_nodes;
    uint32_t num_tiers;
    vector<uint32_t> nodes_per_tier;

    // Export methods
    string to_dot();            // GraphViz DOT format
    string to_graphml();        // GraphML XML format
    string to_json();           // JSON for programmatic use
};

// DOT format example output:
// digraph Topology {
//   // Physical nodes
//   node0 [label="GPU-0" shape=box];
//   node1 [label="GPU-1" shape=box];
//   ...
//   // Hidden nodes
//   hv0 [label="NVSwitch-0" shape=diamond style=dashed];
//   hv1 [label="ToR-0" shape=diamond style=dashed];
//   ...
//   // Edges with bandwidth labels
//   node0 -> hv0 [label="450 GB/s"];
//   node1 -> hv0 [label="450 GB/s"];
//   hv0 -> hv1 [label="100 GB/s"];
// }
```

---

#### Configuration

```cpp
struct TopologyInferenceConfig {
    // Tier thresholds (nanoseconds)
    TierConfig tier_config;

    // Clustering parameters
    enum LinkageType { SINGLE, COMPLETE, AVERAGE };
    LinkageType linkage = SINGLE;

    // Hidden node inference
    uint32_t min_cluster_size_for_switch = 2;  // Min nodes to infer switch
    double min_confidence_threshold = 0.5;      // Discard low-confidence HV

    // Shared bottleneck detection
    bool enable_bottleneck_detection = false;   // Requires Phase 2 data
    double degradation_threshold = 0.20;        // 20% BW drop = shared

    // Output
    bool emit_dot = true;
    bool emit_graphml = false;
    bool emit_json = true;
};
```

### 3.b Graph Visualization

**Export Formats:**

| Format | Use Case |
|--------|----------|
| DOT (GraphViz) | Visual rendering, debugging |
| GraphML | Interoperability, graph analysis tools |
| JSON | Programmatic consumption, web UI |

**Visualization Conventions:**

```
Physical nodes:    [box]      solid border
Hidden nodes:      <diamond>  dashed border
Tier 0 edges:      thick, green (high bandwidth)
Tier 1 edges:      medium, blue
Tier 2+ edges:     thin, gray
Bottleneck edges:  red, annotated with capacity
```

**Example Rendering:**

```
                    ┌─────────────┐
                    │  Spine-0    │ (HV, Tier 2)
                    └──────┬──────┘
                           │ 25 GB/s
              ┌────────────┴────────────┐
              │                         │
        ┌─────┴─────┐             ┌─────┴─────┐
        │   ToR-0   │             │   ToR-1   │ (HV, Tier 1)
        └─────┬─────┘             └─────┬─────┘
              │ 100 GB/s                │ 100 GB/s
      ┌───────┼───────┐         ┌───────┼───────┐
      │       │       │         │       │       │
   ┌──┴──┐ ┌──┴──┐ ┌──┴──┐   ┌──┴──┐ ┌──┴──┐ ┌──┴──┐
   │NVS-0│ │NVS-1│ │NVS-2│   │NVS-3│ │NVS-4│ │NVS-5│ (HV, Tier 0)
   └──┬──┘ └──┬──┘ └──┬──┘   └──┬──┘ └──┬──┘ └──┬──┘
      │       │       │         │       │       │
    ┌─┴─┐   ┌─┴─┐   ┌─┴─┐     ┌─┴─┐   ┌─┴─┐   ┌─┴─┐
    │0-3│   │4-7│   │8-B│     │C-F│   │...│   │...│  (Physical GPUs)
    └───┘   └───┘   └───┘     └───┘   └───┘   └───┘
              450 GB/s each
```

---