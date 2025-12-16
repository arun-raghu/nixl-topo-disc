#include "dendrogram.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <functional>

namespace nixl_topo {

Dendrogram Dendrogram::build(const LatencyMatrix& latencies, LinkageType linkage) {
    Dendrogram result;
    size_t n = latencies.num_nodes();

    if (n == 0) {
        return result;
    }

    result.num_leaves_ = n;

    // Reserve space: n leaves + (n-1) internal nodes = 2n - 1 total
    result.nodes_.reserve(2 * n - 1);

    // Step 1: Initialize each node as its own cluster (leaves)
    for (size_t i = 0; i < n; ++i) {
        ClusterNode leaf;
        leaf.id = static_cast<uint32_t>(i);
        leaf.left_child = -1;
        leaf.right_child = -1;
        leaf.merge_distance = 0.0;
        leaf.members = {static_cast<uint32_t>(i)};
        result.nodes_.push_back(leaf);
    }

    // Special case: single node
    if (n == 1) {
        return result;
    }

    // Step 2: Build distance matrix (full matrix for simplicity)
    std::vector<double> dist_matrix(n * n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) {
                dist_matrix[i * n + j] = std::numeric_limits<double>::infinity();
            } else {
                dist_matrix[i * n + j] = latencies.distance(i, j);
            }
        }
    }

    // Step 3: Track active clusters
    // active[i] = true means cluster i is still active
    // cluster_node_index[i] = index in nodes_ vector for cluster i
    std::vector<bool> active(n, true);
    std::vector<size_t> cluster_node_index(n);
    std::iota(cluster_node_index.begin(), cluster_node_index.end(), 0);

    // Step 4: Iteratively merge closest clusters
    for (size_t merge = 0; merge < n - 1; ++merge) {
        // Find minimum distance pair among active clusters
        double min_dist = std::numeric_limits<double>::max();
        size_t best_i = 0, best_j = 0;

        for (size_t i = 0; i < n; ++i) {
            if (!active[i]) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (!active[j]) continue;
                double d = dist_matrix[i * n + j];
                if (d < min_dist) {
                    min_dist = d;
                    best_i = i;
                    best_j = j;
                }
            }
        }

        // Create new internal node
        ClusterNode internal;
        internal.id = static_cast<uint32_t>(result.nodes_.size());
        internal.left_child = static_cast<int32_t>(cluster_node_index[best_i]);
        internal.right_child = static_cast<int32_t>(cluster_node_index[best_j]);
        internal.merge_distance = min_dist;

        // Merge members from both children
        const auto& left_members = result.nodes_[cluster_node_index[best_i]].members;
        const auto& right_members = result.nodes_[cluster_node_index[best_j]].members;
        internal.members.reserve(left_members.size() + right_members.size());
        internal.members.insert(internal.members.end(), left_members.begin(), left_members.end());
        internal.members.insert(internal.members.end(), right_members.begin(), right_members.end());

        result.nodes_.push_back(internal);

        // Update distance matrix based on linkage type
        for (size_t k = 0; k < n; ++k) {
            if (!active[k] || k == best_i || k == best_j) continue;

            double dist_i_k = dist_matrix[best_i * n + k];
            double dist_j_k = dist_matrix[best_j * n + k];
            double new_dist;

            switch (linkage) {
                case LinkageType::SINGLE:
                    // Minimum distance between clusters
                    new_dist = std::min(dist_i_k, dist_j_k);
                    break;
                case LinkageType::COMPLETE:
                    // Maximum distance between clusters
                    new_dist = std::max(dist_i_k, dist_j_k);
                    break;
                case LinkageType::AVERAGE:
                    // Weighted average based on cluster sizes
                    {
                        size_t size_i = result.nodes_[cluster_node_index[best_i]].members.size();
                        size_t size_j = result.nodes_[cluster_node_index[best_j]].members.size();
                        new_dist = (dist_i_k * size_i + dist_j_k * size_j) /
                                   static_cast<double>(size_i + size_j);
                    }
                    break;
                default:
                    new_dist = std::min(dist_i_k, dist_j_k);
            }

            // Update distances symmetrically
            dist_matrix[best_i * n + k] = new_dist;
            dist_matrix[k * n + best_i] = new_dist;
        }

        // Deactivate j, reuse i for merged cluster
        active[best_j] = false;
        cluster_node_index[best_i] = result.nodes_.size() - 1;
    }

    return result;
}

std::vector<std::vector<uint32_t>> Dendrogram::cut_at_threshold(double threshold) const {
    if (nodes_.empty()) {
        return {};
    }

    std::vector<std::vector<uint32_t>> clusters;

    // DFS traversal: collect clusters where merge_distance > threshold
    // or at leaves
    std::function<void(size_t)> collect_cluster = [&](size_t node_idx) {
        const ClusterNode& node = nodes_[node_idx];

        if (node.is_leaf()) {
            // Leaf node is its own cluster
            clusters.push_back(node.members);
        } else if (node.merge_distance > threshold) {
            // This merge happened at distance > threshold
            // So each child subtree is a separate cluster
            collect_cluster(static_cast<size_t>(node.left_child));
            collect_cluster(static_cast<size_t>(node.right_child));
        } else {
            // This merge happened at distance <= threshold
            // All members belong to same cluster
            clusters.push_back(node.members);
        }
    };

    collect_cluster(root_index());
    return clusters;
}

double Dendrogram::max_distance() const {
    if (nodes_.empty()) {
        return 0.0;
    }
    return nodes_[root_index()].merge_distance;
}

} // namespace nixl_topo
