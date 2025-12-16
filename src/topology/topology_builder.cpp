#include "topology_builder.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace nixl_topo {

TopologyBuilder::TopologyBuilder() : config_() {}

TopologyBuilder::TopologyBuilder(const TierConfig& config) : config_(config) {}

TopologyBuilder TopologyBuilder::from_config(const std::string& config_path) {
    return TopologyBuilder(TierConfig::from_json(config_path));
}

TopologyGraph TopologyBuilder::build(const LatencyMatrix& latencies) {
    TopologyGraph graph;

    if (latencies.empty()) {
        return graph;
    }

    // Step 1: Build dendrogram using hierarchical clustering
    Dendrogram dendrogram = Dendrogram::build(latencies, config_.linkage);

    // Step 2: Assign tiers to nodes
    std::vector<TierAssignment> tier_assignments = assign_tiers(dendrogram, latencies);

    // Step 3: Create physical nodes
    for (const auto& ta : tier_assignments) {
        PhysicalNode node(ta.node_id, ta.tier, ta.cluster_id);
        graph.add_physical_node(node);
    }

    // Step 4: Infer hidden nodes
    std::vector<HiddenNode> hidden_nodes = infer_hidden_nodes(tier_assignments, dendrogram);
    for (const auto& hn : hidden_nodes) {
        graph.add_hidden_node(hn);
    }

    // Step 5: Construct edges
    std::vector<Edge> edges = construct_edges(graph.physical_nodes(),
                                               graph.hidden_nodes(),
                                               latencies);
    for (const auto& edge : edges) {
        graph.add_edge(edge);
    }

    return graph;
}

TopologyGraph TopologyBuilder::build_from_csv(const std::string& csv_path) {
    LatencyMatrix latencies = LatencyMatrix::from_csv(csv_path);
    latencies.symmetrize();
    return build(latencies);
}

std::vector<TopologyBuilder::TierAssignment> TopologyBuilder::assign_tiers(
    const Dendrogram& dendrogram,
    const LatencyMatrix& latencies) {

    size_t n = latencies.num_nodes();
    std::vector<TierAssignment> assignments(n);

    // Initialize with highest tier (nodes not clustered with anyone)
    uint32_t max_tier = static_cast<uint32_t>(config_.tier_thresholds.size());
    for (size_t i = 0; i < n; ++i) {
        assignments[i].node_id = static_cast<uint32_t>(i);
        assignments[i].tier = max_tier;
        assignments[i].cluster_id = static_cast<uint32_t>(i);  // Each node in own cluster initially
    }

    // For each tier threshold (from lowest/tightest to highest/loosest)
    for (uint32_t tier = 0; tier < config_.tier_thresholds.size(); ++tier) {
        double threshold = static_cast<double>(config_.tier_thresholds[tier]);

        // Cut dendrogram at this threshold
        auto clusters = dendrogram.cut_at_threshold(threshold);

        // Assign tier to nodes that cluster together at this threshold
        uint32_t cluster_id = 0;
        for (const auto& cluster : clusters) {
            if (cluster.size() > 1) {
                // Multiple nodes cluster together at this tier
                for (uint32_t node_id : cluster) {
                    // Only update if this is a tighter grouping (lower tier)
                    if (tier < assignments[node_id].tier) {
                        assignments[node_id].tier = tier;
                        assignments[node_id].cluster_id = cluster_id;
                    }
                }
            }
            ++cluster_id;
        }
    }

    // Normalize cluster IDs within each tier
    std::map<uint32_t, std::map<uint32_t, uint32_t>> tier_cluster_map;
    for (auto& ta : assignments) {
        auto& cluster_remap = tier_cluster_map[ta.tier];
        if (cluster_remap.find(ta.cluster_id) == cluster_remap.end()) {
            cluster_remap[ta.cluster_id] = static_cast<uint32_t>(cluster_remap.size());
        }
        ta.cluster_id = cluster_remap[ta.cluster_id];
    }

    return assignments;
}

std::vector<HiddenNode> TopologyBuilder::infer_hidden_nodes(
    const std::vector<TierAssignment>& tier_assignments,
    const Dendrogram& dendrogram) {

    std::vector<HiddenNode> hidden_nodes;

    // Group nodes by (tier, cluster_id)
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> clusters;
    for (const auto& ta : tier_assignments) {
        clusters[{ta.tier, ta.cluster_id}].push_back(ta.node_id);
    }

    uint32_t hv_id = 0;
    for (const auto& [key, members] : clusters) {
        auto [tier, cluster_id] = key;

        // Only create hidden node if cluster has multiple members
        if (members.size() >= config_.min_cluster_size_for_switch) {
            HiddenNodeType type = infer_hidden_type(tier);
            std::string label = std::string(to_string(type)) + "-" + std::to_string(cluster_id);

            // Calculate confidence based on cluster tightness
            // For now, use a simple heuristic based on cluster size
            double confidence = std::min(0.95, 0.5 + 0.1 * members.size());

            hidden_nodes.emplace_back(hv_id, type, tier, label, confidence);
            ++hv_id;
        }
    }

    return hidden_nodes;
}

std::vector<Edge> TopologyBuilder::construct_edges(
    const std::vector<PhysicalNode>& physical_nodes,
    const std::vector<HiddenNode>& hidden_nodes,
    const LatencyMatrix& latencies) {

    std::vector<Edge> edges;

    // Build map from (tier, cluster_id) to hidden node index
    std::map<std::pair<uint32_t, uint32_t>, size_t> cluster_to_hidden;
    for (size_t i = 0; i < hidden_nodes.size(); ++i) {
        // Need to figure out which cluster this hidden node represents
        // For now, hidden nodes are indexed in order of (tier, cluster_id)
    }

    // Group physical nodes by (tier, cluster_id)
    std::map<std::pair<uint32_t, uint32_t>, std::vector<const PhysicalNode*>> clusters;
    for (const auto& pn : physical_nodes) {
        clusters[{pn.tier, pn.cluster_id}].push_back(&pn);
    }

    // Match clusters to hidden nodes
    size_t hidden_idx = 0;
    for (const auto& [key, members] : clusters) {
        if (members.size() >= config_.min_cluster_size_for_switch && hidden_idx < hidden_nodes.size()) {
            cluster_to_hidden[key] = hidden_idx;
            ++hidden_idx;
        }
    }

    // Create edges from physical nodes to their cluster's hidden node
    for (const auto& pn : physical_nodes) {
        auto it = cluster_to_hidden.find({pn.tier, pn.cluster_id});
        if (it != cluster_to_hidden.end()) {
            const HiddenNode& hn = hidden_nodes[it->second];

            // Calculate average latency from this node to others in cluster
            uint64_t avg_latency = 0;
            size_t count = 0;
            for (const auto& other : physical_nodes) {
                if (other.tier == pn.tier && other.cluster_id == pn.cluster_id &&
                    other.node_id != pn.node_id) {
                    avg_latency += static_cast<uint64_t>(latencies.distance(pn.node_id, other.node_id));
                    ++count;
                }
            }
            if (count > 0) {
                avg_latency /= count;
                // Edge latency is half the round-trip (approximation)
                avg_latency /= 2;
            }

            Edge edge(pn.node_id, hn.hv_id, false, true, avg_latency, EdgeType::SWITCHED);
            edges.push_back(edge);
        }
    }

    // Create edges between hidden nodes at different tiers
    // Connect lower-tier hidden nodes to higher-tier hidden nodes
    std::map<uint32_t, std::vector<size_t>> hidden_by_tier;
    for (size_t i = 0; i < hidden_nodes.size(); ++i) {
        hidden_by_tier[hidden_nodes[i].tier].push_back(i);
    }

    // For simplicity, connect all hidden nodes at tier T to a single node at tier T+1
    // This is a simplification - real topology inference would be more sophisticated
    uint32_t max_tier = 0;
    for (const auto& hn : hidden_nodes) {
        max_tier = std::max(max_tier, hn.tier);
    }

    for (uint32_t tier = 0; tier < max_tier; ++tier) {
        if (hidden_by_tier.find(tier) != hidden_by_tier.end() &&
            hidden_by_tier.find(tier + 1) != hidden_by_tier.end()) {

            const auto& lower_tier_nodes = hidden_by_tier[tier];
            const auto& upper_tier_nodes = hidden_by_tier[tier + 1];

            // Connect each lower tier node to the first upper tier node
            // (simplified - real implementation would use latency data)
            if (!upper_tier_nodes.empty()) {
                for (size_t lower_idx : lower_tier_nodes) {
                    uint64_t inter_tier_latency = 0;
                    if (tier + 1 < config_.tier_thresholds.size()) {
                        inter_tier_latency = config_.tier_thresholds[tier + 1] / 2;
                    } else {
                        inter_tier_latency = 50000;  // Default 50us
                    }

                    Edge edge(static_cast<uint32_t>(hidden_nodes[lower_idx].hv_id),
                             static_cast<uint32_t>(hidden_nodes[upper_tier_nodes[0]].hv_id),
                             true, true, inter_tier_latency, EdgeType::SWITCHED);
                    edges.push_back(edge);
                }
            }
        }
    }

    return edges;
}

double TopologyBuilder::calculate_confidence(
    const std::vector<uint32_t>& cluster_members,
    const LatencyMatrix& latencies) {

    if (cluster_members.size() < 2) {
        return 0.0;
    }

    // Calculate variance of intra-cluster latencies
    double sum = 0.0;
    double sum_sq = 0.0;
    size_t count = 0;

    for (size_t i = 0; i < cluster_members.size(); ++i) {
        for (size_t j = i + 1; j < cluster_members.size(); ++j) {
            double d = latencies.distance(cluster_members[i], cluster_members[j]);
            sum += d;
            sum_sq += d * d;
            ++count;
        }
    }

    if (count == 0) {
        return 0.0;
    }

    double mean = sum / count;
    double variance = (sum_sq / count) - (mean * mean);
    double stddev = std::sqrt(std::max(0.0, variance));

    // Coefficient of variation (lower = tighter cluster = higher confidence)
    double cv = (mean > 0) ? (stddev / mean) : 1.0;

    // Convert to confidence score (0.0 - 1.0)
    // CV of 0 -> confidence 1.0
    // CV of 1 -> confidence 0.5
    // CV of 2+ -> confidence approaching 0
    double confidence = 1.0 / (1.0 + cv);

    return std::max(config_.min_confidence_threshold, confidence);
}

HiddenNodeType TopologyBuilder::infer_hidden_type(uint32_t tier) {
    switch (tier) {
        case 0:
            return HiddenNodeType::NVSWITCH;  // Intra-node interconnect
        case 1:
            return HiddenNodeType::TOR_SWITCH;  // Top-of-rack
        case 2:
            return HiddenNodeType::SPINE_SWITCH;  // Spine/aggregation
        default:
            return HiddenNodeType::UNKNOWN_BOTTLENECK;
    }
}

} // namespace nixl_topo
