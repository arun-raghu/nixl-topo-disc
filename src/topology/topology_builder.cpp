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

    // Step 6: Store latency matrix for visualization
    graph.set_latency_matrix(latencies);

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

    (void)tier_assignments;  // We'll use dendrogram directly for hierarchical structure

    std::vector<HiddenNode> hidden_nodes;

    // Create hidden nodes at EACH tier level by cutting the dendrogram
    // at each threshold and identifying clusters that form at that level
    uint32_t hv_id = 0;

    for (uint32_t tier = 0; tier <= config_.tier_thresholds.size(); ++tier) {
        double threshold;
        if (tier < config_.tier_thresholds.size()) {
            threshold = static_cast<double>(config_.tier_thresholds[tier]);
        } else {
            // For the highest tier, use infinity to get all remaining merges
            threshold = dendrogram.max_distance() + 1.0;
        }

        auto clusters = dendrogram.cut_at_threshold(threshold);

        // Track which clusters at this tier have multiple members
        // and weren't already fully formed at a lower tier
        uint32_t cluster_idx = 0;
        for (const auto& cluster : clusters) {
            if (cluster.size() >= config_.min_cluster_size_for_switch) {
                // Check if this cluster is NEW at this tier (not already formed at lower tier)
                bool is_new_cluster = false;

                if (tier == 0) {
                    // All clusters at tier 0 are new
                    is_new_cluster = true;
                } else {
                    // Check if this cluster was split at the previous tier
                    double prev_threshold = static_cast<double>(config_.tier_thresholds[tier - 1]);
                    auto prev_clusters = dendrogram.cut_at_threshold(prev_threshold);

                    // This cluster is new if it wasn't a single cluster at prev tier
                    bool found_as_single = false;
                    for (const auto& prev_cluster : prev_clusters) {
                        if (prev_cluster.size() == cluster.size()) {
                            // Check if same members
                            std::set<uint32_t> curr_set(cluster.begin(), cluster.end());
                            std::set<uint32_t> prev_set(prev_cluster.begin(), prev_cluster.end());
                            if (curr_set == prev_set) {
                                found_as_single = true;
                                break;
                            }
                        }
                    }
                    is_new_cluster = !found_as_single;
                }

                if (is_new_cluster) {
                    HiddenNodeType type = infer_hidden_type(tier);
                    std::string label = std::string(to_string(type)) + "-" + std::to_string(cluster_idx);

                    double confidence = std::min(0.95, 0.5 + 0.1 * cluster.size());

                    // Hidden nodes are tier+1 since physical nodes are tier 0
                    hidden_nodes.emplace_back(hv_id, type, tier + 1, label, confidence);
                    ++hv_id;
                }
            }
            ++cluster_idx;
        }
    }

    return hidden_nodes;
}

std::vector<Edge> TopologyBuilder::construct_edges(
    const std::vector<PhysicalNode>& physical_nodes,
    const std::vector<HiddenNode>& hidden_nodes,
    const LatencyMatrix& latencies) {

    std::vector<Edge> edges;

    if (hidden_nodes.empty()) {
        return edges;
    }

    // Group hidden nodes by tier
    std::map<uint32_t, std::vector<size_t>> hidden_by_tier;
    for (size_t i = 0; i < hidden_nodes.size(); ++i) {
        hidden_by_tier[hidden_nodes[i].tier].push_back(i);
    }

    // Find the lowest tier with hidden nodes - physical nodes connect here
    uint32_t lowest_tier = hidden_nodes[0].tier;
    for (const auto& hn : hidden_nodes) {
        lowest_tier = std::min(lowest_tier, hn.tier);
    }

    // We need to rebuild cluster membership to match hidden nodes to physical nodes
    // Build dendrogram to get cluster info (we don't have it passed in)
    Dendrogram dendrogram = Dendrogram::build(latencies, config_.linkage);

    // Get clusters at each tier and map to hidden nodes
    // hidden_node_members[hv_id] = set of physical node IDs in that hidden node's cluster
    std::map<uint32_t, std::set<uint32_t>> hidden_node_members;

    size_t hv_idx = 0;
    for (uint32_t tier = 0; tier <= config_.tier_thresholds.size() && hv_idx < hidden_nodes.size(); ++tier) {
        double threshold;
        if (tier < config_.tier_thresholds.size()) {
            threshold = static_cast<double>(config_.tier_thresholds[tier]);
        } else {
            threshold = dendrogram.max_distance() + 1.0;
        }

        auto clusters = dendrogram.cut_at_threshold(threshold);

        for (const auto& cluster : clusters) {
            if (cluster.size() >= config_.min_cluster_size_for_switch) {
                // Check if this is a NEW cluster at this tier
                bool is_new_cluster = false;

                if (tier == 0) {
                    is_new_cluster = true;
                } else {
                    double prev_threshold = static_cast<double>(config_.tier_thresholds[tier - 1]);
                    auto prev_clusters = dendrogram.cut_at_threshold(prev_threshold);

                    bool found_as_single = false;
                    for (const auto& prev_cluster : prev_clusters) {
                        if (prev_cluster.size() == cluster.size()) {
                            std::set<uint32_t> curr_set(cluster.begin(), cluster.end());
                            std::set<uint32_t> prev_set(prev_cluster.begin(), prev_cluster.end());
                            if (curr_set == prev_set) {
                                found_as_single = true;
                                break;
                            }
                        }
                    }
                    is_new_cluster = !found_as_single;
                }

                if (is_new_cluster && hv_idx < hidden_nodes.size()) {
                    hidden_node_members[hidden_nodes[hv_idx].hv_id] =
                        std::set<uint32_t>(cluster.begin(), cluster.end());
                    ++hv_idx;
                }
            }
        }
    }

    // Connect physical nodes to their lowest-tier hidden node
    for (const auto& pn : physical_nodes) {
        // Find which tier-0 hidden node contains this physical node
        for (const auto& [hv_id, members] : hidden_node_members) {
            const HiddenNode& hn = hidden_nodes[hv_id];
            if (hn.tier == lowest_tier && members.count(pn.node_id)) {
                // Calculate average latency to other nodes in this cluster
                uint64_t avg_latency = 0;
                size_t count = 0;
                for (uint32_t other_id : members) {
                    if (other_id != pn.node_id) {
                        avg_latency += static_cast<uint64_t>(latencies.distance(pn.node_id, other_id));
                        ++count;
                    }
                }
                if (count > 0) {
                    avg_latency /= count;
                    avg_latency /= 2;  // Half for one-way
                }

                Edge edge(pn.node_id, hv_id, false, true, avg_latency, EdgeType::SWITCHED);
                edges.push_back(edge);
                break;
            }
        }
    }

    // Connect hidden nodes across tiers
    // A lower-tier hidden node connects to a higher-tier hidden node if
    // the higher-tier node's members are a superset of the lower-tier node's members
    uint32_t max_tier = 0;
    for (const auto& hn : hidden_nodes) {
        max_tier = std::max(max_tier, hn.tier);
    }

    for (uint32_t tier = lowest_tier; tier < max_tier; ++tier) {
        if (hidden_by_tier.find(tier) == hidden_by_tier.end() ||
            hidden_by_tier.find(tier + 1) == hidden_by_tier.end()) {
            continue;
        }

        for (size_t lower_idx : hidden_by_tier[tier]) {
            const HiddenNode& lower_hn = hidden_nodes[lower_idx];
            const auto& lower_members = hidden_node_members[lower_hn.hv_id];

            // Find the parent hidden node at tier+1 that contains all our members
            for (size_t upper_idx : hidden_by_tier[tier + 1]) {
                const HiddenNode& upper_hn = hidden_nodes[upper_idx];
                const auto& upper_members = hidden_node_members[upper_hn.hv_id];

                // Check if upper_members is a superset of lower_members
                bool is_superset = true;
                for (uint32_t m : lower_members) {
                    if (upper_members.find(m) == upper_members.end()) {
                        is_superset = false;
                        break;
                    }
                }

                if (is_superset) {
                    // Calculate inter-tier latency from actual measured latencies
                    // Find nodes in upper cluster but NOT in lower cluster (sibling clusters)
                    std::set<uint32_t> sibling_members;
                    for (uint32_t m : upper_members) {
                        if (lower_members.find(m) == lower_members.end()) {
                            sibling_members.insert(m);
                        }
                    }

                    uint64_t inter_tier_latency = 0;
                    if (!sibling_members.empty()) {
                        // Calculate average cross-cluster latency (between lower and sibling clusters)
                        double total_cross_latency = 0.0;
                        size_t cross_count = 0;
                        for (uint32_t lower_node : lower_members) {
                            for (uint32_t sibling_node : sibling_members) {
                                total_cross_latency += latencies.distance(lower_node, sibling_node);
                                ++cross_count;
                            }
                        }

                        if (cross_count > 0) {
                            double avg_cross = total_cross_latency / cross_count;

                            // Calculate path length from physical node to this hidden node
                            // by summing edge weights of the path we've already computed
                            double path_to_lower = 0.0;

                            // Find any physical node in lower_members and trace path to lower_hn
                            uint32_t sample_node = *lower_members.begin();

                            // Sum edges from sample_node up to lower_hn
                            for (const auto& e : edges) {
                                if (!e.src_is_hidden && e.dst_is_hidden) {
                                    // Physical to hidden edge
                                    if (e.src_id == sample_node) {
                                        path_to_lower += e.latency_ns;
                                        // Now find path from that hidden node to lower_hn
                                        uint32_t current_hv = e.dst_id;
                                        while (current_hv != lower_hn.hv_id) {
                                            bool found = false;
                                            for (const auto& e2 : edges) {
                                                if (e2.src_is_hidden && e2.dst_is_hidden &&
                                                    e2.src_id == current_hv) {
                                                    path_to_lower += e2.latency_ns;
                                                    current_hv = e2.dst_id;
                                                    found = true;
                                                    break;
                                                }
                                            }
                                            if (!found) break;
                                        }
                                        break;
                                    }
                                }
                            }

                            // Edge latency = (cross_latency - 2*path_to_lower) / 2
                            // Path: P_a -> ... -> lower_hn -> upper_hn -> ... -> P_b
                            // Total = path_to_lower + edge + edge + path_to_sibling
                            // Assuming symmetric: path_to_sibling ≈ path_to_lower
                            inter_tier_latency = static_cast<uint64_t>(
                                std::max(0.0, (avg_cross - 2.0 * path_to_lower) / 2.0));
                        }
                    }

                    Edge edge(lower_hn.hv_id, upper_hn.hv_id, true, true,
                             inter_tier_latency, EdgeType::SWITCHED);
                    edges.push_back(edge);
                    break;  // Found parent, move to next lower node
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
            return HiddenNodeType::TIER0_SWITCH;  // Intra-node interconnect
        case 1:
            return HiddenNodeType::TOR_SWITCH;  // Top-of-rack
        case 2:
            return HiddenNodeType::SPINE_SWITCH;  // Spine/aggregation
        default:
            return HiddenNodeType::UNKNOWN_BOTTLENECK;
    }
}

} // namespace nixl_topo
