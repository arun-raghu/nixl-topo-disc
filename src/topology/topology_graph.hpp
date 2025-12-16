#pragma once

#include "tier_config.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nixl_topo {

// =============================================================================
// Physical Node
// =============================================================================

/// Physical node in the topology (represents an agent/GPU/NIC)
struct PhysicalNode {
    uint32_t node_id;                 // Unique node ID
    std::string hostname;             // Hostname (may be empty)
    NodeType type = NodeType::UNKNOWN;
    uint32_t tier;                    // Assigned topology tier (0 = closest)
    uint32_t cluster_id;              // Cluster ID within the tier

    PhysicalNode()
        : node_id(0), tier(0), cluster_id(0) {}

    PhysicalNode(uint32_t id, uint32_t t, uint32_t c)
        : node_id(id), tier(t), cluster_id(c) {}
};

// =============================================================================
// Hidden Node
// =============================================================================

/// Hidden (inferred) node in the topology (represents switches, bottlenecks)
struct HiddenNode {
    uint32_t hv_id;                   // Unique hidden node ID
    HiddenNodeType type;              // Type of hidden node
    uint32_t tier;                    // Topology tier level
    std::string inferred_label;       // Label (e.g., "NVSwitch-0", "ToR-0")
    double confidence;                // Confidence score (0.0-1.0)

    HiddenNode()
        : hv_id(0), type(HiddenNodeType::UNKNOWN_BOTTLENECK),
          tier(0), confidence(0.0) {}

    HiddenNode(uint32_t id, HiddenNodeType t, uint32_t tier_level,
               const std::string& label, double conf)
        : hv_id(id), type(t), tier(tier_level),
          inferred_label(label), confidence(conf) {}
};

// =============================================================================
// Edge
// =============================================================================

/// Edge connecting nodes in the topology
struct Edge {
    uint32_t src_id;                  // Source node ID
    uint32_t dst_id;                  // Destination node ID
    bool src_is_hidden;               // True if source is a hidden node
    bool dst_is_hidden;               // True if destination is a hidden node
    double bandwidth_gbps;            // Bandwidth in GB/s (may be estimated)
    uint64_t latency_ns;              // Latency in nanoseconds
    EdgeType type;                    // Edge type

    Edge()
        : src_id(0), dst_id(0), src_is_hidden(false), dst_is_hidden(false),
          bandwidth_gbps(0.0), latency_ns(0), type(EdgeType::SWITCHED) {}

    Edge(uint32_t src, uint32_t dst, bool src_hidden, bool dst_hidden,
         uint64_t lat, EdgeType edge_type = EdgeType::SWITCHED)
        : src_id(src), dst_id(dst), src_is_hidden(src_hidden),
          dst_is_hidden(dst_hidden), bandwidth_gbps(0.0),
          latency_ns(lat), type(edge_type) {}
};

// =============================================================================
// Topology Graph
// =============================================================================

/// Complete topology graph with physical nodes, hidden nodes, and edges
class TopologyGraph {
public:
    TopologyGraph() = default;

    // Node management
    void add_physical_node(const PhysicalNode& node);
    void add_hidden_node(const HiddenNode& node);
    void add_edge(const Edge& edge);

    // Accessors
    const std::vector<PhysicalNode>& physical_nodes() const { return physical_nodes_; }
    const std::vector<HiddenNode>& hidden_nodes() const { return hidden_nodes_; }
    const std::vector<Edge>& edges() const { return edges_; }

    std::vector<PhysicalNode>& physical_nodes() { return physical_nodes_; }
    std::vector<HiddenNode>& hidden_nodes() { return hidden_nodes_; }
    std::vector<Edge>& edges() { return edges_; }

    // Export formats
    /// Export to adjacency list text format
    std::string to_adjacency_list() const;

    /// Export to DOT format for GraphViz visualization
    std::string to_dot() const;

    /// Export to JSON format
    std::string to_json() const;

    // Utilities
    bool empty() const { return physical_nodes_.empty(); }
    size_t num_physical_nodes() const { return physical_nodes_.size(); }
    size_t num_hidden_nodes() const { return hidden_nodes_.size(); }
    size_t num_edges() const { return edges_.size(); }

private:
    std::vector<PhysicalNode> physical_nodes_;
    std::vector<HiddenNode> hidden_nodes_;
    std::vector<Edge> edges_;
};

} // namespace nixl_topo
