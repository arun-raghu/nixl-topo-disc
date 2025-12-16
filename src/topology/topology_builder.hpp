#pragma once

#include "tier_config.hpp"
#include "latency_matrix.hpp"
#include "dendrogram.hpp"
#include "topology_graph.hpp"

#include <string>

namespace nixl_topo {

// =============================================================================
// Topology Builder
// =============================================================================

/// Builds topology graphs from latency matrices using hierarchical clustering
class TopologyBuilder {
public:
    /// Construct with default configuration
    TopologyBuilder();

    /// Construct with custom configuration
    explicit TopologyBuilder(const TierConfig& config);

    /// Construct from JSON configuration file
    /// @param config_path Path to JSON config file (uses defaults if empty)
    /// @return TopologyBuilder with loaded configuration
    static TopologyBuilder from_config(const std::string& config_path);

    /// Build topology graph from latency matrix
    /// @param latencies NxN latency matrix
    /// @return Topology graph with physical nodes, hidden nodes, and edges
    TopologyGraph build(const LatencyMatrix& latencies);

    /// Build topology graph from CSV file
    /// @param csv_path Path to CSV latency matrix file
    /// @return Topology graph with physical nodes, hidden nodes, and edges
    TopologyGraph build_from_csv(const std::string& csv_path);

    /// Get current configuration
    const TierConfig& config() const { return config_; }

    /// Set configuration
    void set_config(const TierConfig& config) { config_ = config; }

private:
    TierConfig config_;

    /// Assign tiers to nodes based on dendrogram structure
    /// @param dendrogram Clustering dendrogram
    /// @param latencies Original latency matrix (for distance calculations)
    /// @return Vector of (node_id, tier, cluster_id) assignments
    struct TierAssignment {
        uint32_t node_id;
        uint32_t tier;
        uint32_t cluster_id;
    };
    std::vector<TierAssignment> assign_tiers(const Dendrogram& dendrogram,
                                             const LatencyMatrix& latencies);

    /// Infer hidden nodes from tier assignments
    /// @param tier_assignments Node tier/cluster assignments
    /// @param dendrogram Clustering dendrogram
    /// @return Vector of inferred hidden nodes
    std::vector<HiddenNode> infer_hidden_nodes(
        const std::vector<TierAssignment>& tier_assignments,
        const Dendrogram& dendrogram);

    /// Construct edges between physical and hidden nodes
    /// @param physical_nodes Physical nodes with tier assignments
    /// @param hidden_nodes Inferred hidden nodes
    /// @param latencies Original latency matrix
    /// @return Vector of edges
    std::vector<Edge> construct_edges(
        const std::vector<PhysicalNode>& physical_nodes,
        const std::vector<HiddenNode>& hidden_nodes,
        const LatencyMatrix& latencies);

    /// Calculate confidence score for a cluster
    /// @param cluster_members Node IDs in the cluster
    /// @param latencies Latency matrix
    /// @return Confidence score (0.0-1.0) based on cluster tightness
    double calculate_confidence(const std::vector<uint32_t>& cluster_members,
                               const LatencyMatrix& latencies);

    /// Determine hidden node type based on tier level
    HiddenNodeType infer_hidden_type(uint32_t tier);
};

} // namespace nixl_topo
