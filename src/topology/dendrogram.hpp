#pragma once

#include "latency_matrix.hpp"
#include "tier_config.hpp"

#include <cstdint>
#include <vector>

namespace nixl_topo {

// =============================================================================
// Cluster Node (Dendrogram Node)
// =============================================================================

/// Node in the dendrogram - either a leaf (physical node) or internal merge node
struct ClusterNode {
    uint32_t id;                    // Unique node ID
    int32_t left_child;             // Index of left child (-1 if leaf)
    int32_t right_child;            // Index of right child (-1 if leaf)
    double merge_distance;          // Latency at which clusters merged (0 for leaves)
    std::vector<uint32_t> members;  // All leaf node IDs in this cluster

    ClusterNode()
        : id(0), left_child(-1), right_child(-1), merge_distance(0.0) {}

    bool is_leaf() const { return left_child < 0 && right_child < 0; }
};

// =============================================================================
// Dendrogram
// =============================================================================

/// Hierarchical clustering dendrogram built from a latency matrix
class Dendrogram {
public:
    Dendrogram() = default;

    /// Build dendrogram from latency matrix using specified linkage
    /// @param latencies NxN latency matrix
    /// @param linkage Linkage type (SINGLE, COMPLETE, or AVERAGE)
    /// @return Dendrogram with N leaves and N-1 internal nodes
    static Dendrogram build(const LatencyMatrix& latencies,
                            LinkageType linkage = LinkageType::SINGLE);

    /// Get all nodes (leaves at indices [0, N), internal nodes at [N, 2N-1))
    const std::vector<ClusterNode>& nodes() const { return nodes_; }

    /// Get root node index (always 2N-2 for N leaves)
    size_t root_index() const { return nodes_.empty() ? 0 : nodes_.size() - 1; }

    /// Get number of leaf nodes (physical nodes)
    size_t num_leaves() const { return num_leaves_; }

    /// Cut dendrogram at given distance threshold
    /// Returns list of clusters, where each cluster is a vector of leaf node IDs
    /// Nodes that merge at distance <= threshold are grouped together
    /// @param threshold Distance threshold for cutting
    /// @return Vector of clusters (each cluster is a vector of node IDs)
    std::vector<std::vector<uint32_t>> cut_at_threshold(double threshold) const;

    /// Get merge distance at root (maximum distance in dendrogram)
    double max_distance() const;

    /// Check if dendrogram is empty
    bool empty() const { return nodes_.empty(); }

private:
    std::vector<ClusterNode> nodes_;
    size_t num_leaves_ = 0;
};

} // namespace nixl_topo
