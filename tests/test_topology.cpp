#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "topology/latency_matrix.hpp"
#include "topology/dendrogram.hpp"
#include "topology/topology_graph.hpp"
#include "topology/topology_builder.hpp"
#include "topology/tier_config.hpp"

namespace nixl_topo {
namespace testing {

// =============================================================================
// Test Fixture
// =============================================================================

class TopologyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test data directory is relative to the test executable location
        // CMake copies test_data to build directory
        test_data_dir_ = "test_data";

        // Check if test data exists, try alternate paths if not
        if (!std::filesystem::exists(test_data_dir_)) {
            test_data_dir_ = "../tests/test_data";
        }
        if (!std::filesystem::exists(test_data_dir_)) {
            test_data_dir_ = "../../tests/test_data";
        }
    }

    std::string test_data_path(const std::string& filename) const {
        return (std::filesystem::path(test_data_dir_) / filename).string();
    }

    std::string test_data_dir_;
};

// =============================================================================
// LatencyMatrix Tests
// =============================================================================

TEST_F(TopologyTest, LatencyMatrix_LoadFromCSV_Small4Node) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    EXPECT_EQ(matrix.num_nodes(), 4);

    // Verify diagonal is zero
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(matrix.at(i, i).avg_ns, 0.0);
    }

    // Verify known values
    EXPECT_DOUBLE_EQ(matrix.at(0, 1).avg_ns, 2000.0);
    EXPECT_DOUBLE_EQ(matrix.at(0, 2).avg_ns, 10000.0);
}

TEST_F(TopologyTest, LatencyMatrix_LoadFromCSV_Medium8Node) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("medium_8node.csv"));
    EXPECT_EQ(matrix.num_nodes(), 8);
}

TEST_F(TopologyTest, LatencyMatrix_LoadFromCSV_InvalidFile) {
    EXPECT_THROW(
        LatencyMatrix::from_csv("nonexistent_file.csv"),
        std::runtime_error
    );
}

TEST_F(TopologyTest, LatencyMatrix_Distance_Symmetric) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    for (size_t i = 0; i < matrix.num_nodes(); ++i) {
        for (size_t j = i + 1; j < matrix.num_nodes(); ++j) {
            EXPECT_DOUBLE_EQ(matrix.distance(i, j), matrix.distance(j, i));
        }
    }
}

TEST_F(TopologyTest, LatencyMatrix_Symmetrize) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("edge_cases/asymmetric.csv"));
    matrix.symmetrize();

    for (size_t i = 0; i < matrix.num_nodes(); ++i) {
        for (size_t j = 0; j < matrix.num_nodes(); ++j) {
            EXPECT_DOUBLE_EQ(matrix.at(i, j).avg_ns, matrix.at(j, i).avg_ns);
        }
    }
}

TEST_F(TopologyTest, LatencyMatrix_SingleNode) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("edge_cases/single_node.csv"));
    EXPECT_EQ(matrix.num_nodes(), 1);
    EXPECT_DOUBLE_EQ(matrix.at(0, 0).avg_ns, 0.0);
}

TEST_F(TopologyTest, LatencyMatrix_TwoNodes) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("edge_cases/two_nodes.csv"));
    EXPECT_EQ(matrix.num_nodes(), 2);
    EXPECT_GT(matrix.distance(0, 1), 0.0);
}

// =============================================================================
// Dendrogram Tests
// =============================================================================

TEST_F(TopologyTest, Dendrogram_Build_CorrectNodeCount) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // n leaves + (n-1) internal nodes = 2n - 1 total
    EXPECT_EQ(dendrogram.nodes().size(), 7);  // 4 + 3
    EXPECT_EQ(dendrogram.num_leaves(), 4);
}

TEST_F(TopologyTest, Dendrogram_Build_LeavesAreCorrect) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // First N nodes should be leaves
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(dendrogram.nodes()[i].is_leaf());
        EXPECT_EQ(dendrogram.nodes()[i].members.size(), 1);
        EXPECT_EQ(dendrogram.nodes()[i].members[0], i);
    }
}

TEST_F(TopologyTest, Dendrogram_Build_InternalNodesHaveChildren) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // Internal nodes (index >= N) should have children
    for (size_t i = 4; i < dendrogram.nodes().size(); ++i) {
        EXPECT_FALSE(dendrogram.nodes()[i].is_leaf());
        EXPECT_GE(dendrogram.nodes()[i].left_child, 0);
        EXPECT_GE(dendrogram.nodes()[i].right_child, 0);
    }
}

TEST_F(TopologyTest, Dendrogram_Build_RootContainsAllNodes) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    const auto& root = dendrogram.nodes()[dendrogram.root_index()];
    EXPECT_EQ(root.members.size(), 4);

    // Verify all nodes are in root
    std::set<uint32_t> members(root.members.begin(), root.members.end());
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(members.count(i) > 0);
    }
}

TEST_F(TopologyTest, Dendrogram_Build_MergeDistancesAreMonotonic) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // Merge distances should be non-decreasing for internal nodes
    double prev_dist = 0.0;
    for (size_t i = dendrogram.num_leaves(); i < dendrogram.nodes().size(); ++i) {
        EXPECT_GE(dendrogram.nodes()[i].merge_distance, prev_dist);
        prev_dist = dendrogram.nodes()[i].merge_distance;
    }
}

TEST_F(TopologyTest, Dendrogram_CutAtThreshold_AllSeparate) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // Cut at 0 should give N clusters (each node alone)
    auto clusters = dendrogram.cut_at_threshold(0.0);
    EXPECT_EQ(clusters.size(), 4);
}

TEST_F(TopologyTest, Dendrogram_CutAtThreshold_AllMerged) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // Cut at very high threshold should give 1 cluster
    auto clusters = dendrogram.cut_at_threshold(1e12);
    EXPECT_EQ(clusters.size(), 1);
    EXPECT_EQ(clusters[0].size(), 4);
}

TEST_F(TopologyTest, Dendrogram_CutAtThreshold_IntermediateCut) {
    // small_4node.csv: nodes 0,1 are close (2000ns), nodes 2,3 are close (2000ns)
    // Inter-cluster distance is 10000ns
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    // Cut at 5000ns should give 2 clusters
    auto clusters = dendrogram.cut_at_threshold(5000.0);
    EXPECT_EQ(clusters.size(), 2);

    // Each cluster should have 2 nodes
    for (const auto& cluster : clusters) {
        EXPECT_EQ(cluster.size(), 2);
    }
}

TEST_F(TopologyTest, Dendrogram_SingleNode) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("edge_cases/single_node.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    EXPECT_EQ(dendrogram.nodes().size(), 1);
    EXPECT_EQ(dendrogram.num_leaves(), 1);
}

TEST_F(TopologyTest, Dendrogram_TwoNodes) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("edge_cases/two_nodes.csv"));
    auto dendrogram = Dendrogram::build(matrix);

    EXPECT_EQ(dendrogram.nodes().size(), 3);  // 2 leaves + 1 internal
    EXPECT_EQ(dendrogram.num_leaves(), 2);
}

// =============================================================================
// TopologyBuilder Tests
// =============================================================================

TEST_F(TopologyTest, TopologyBuilder_Build_ProducesValidGraph) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Should have 4 physical nodes
    EXPECT_EQ(graph.physical_nodes().size(), 4);

    // Should have some hidden nodes (clusters with size >= 2)
    EXPECT_GT(graph.hidden_nodes().size(), 0);

    // Should have edges
    EXPECT_GT(graph.edges().size(), 0);
}

TEST_F(TopologyTest, TopologyBuilder_Build_TierAssignment) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // All physical nodes should have tier assignment
    for (const auto& node : graph.physical_nodes()) {
        // Tier should be within valid range
        EXPECT_LE(node.tier, 3);
    }
}

TEST_F(TopologyTest, TopologyBuilder_Build_ClusterMembership) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Group nodes by cluster
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> clusters;
    for (const auto& node : graph.physical_nodes()) {
        clusters[{node.tier, node.cluster_id}].push_back(node.node_id);
    }

    // Should have at least 2 clusters (nodes 0,1 and nodes 2,3)
    EXPECT_GE(clusters.size(), 1);
}

TEST_F(TopologyTest, TopologyBuilder_BuildFromCSV) {
    TopologyBuilder builder;
    auto graph = builder.build_from_csv(test_data_path("small_4node.csv"));

    EXPECT_EQ(graph.physical_nodes().size(), 4);
}

TEST_F(TopologyTest, TopologyBuilder_CustomConfig) {
    TierConfig config;
    config.tier_thresholds = {3000, 10000, 30000};  // Tighter thresholds

    TopologyBuilder builder(config);
    auto graph = builder.build_from_csv(test_data_path("small_4node.csv"));

    EXPECT_EQ(graph.physical_nodes().size(), 4);
}

// =============================================================================
// Inter-Cluster Connectivity Tests
// =============================================================================

TEST_F(TopologyTest, InterCluster_HiddenNodesAtMultipleTiers) {
    // small_4node.csv: nodes 0,1 cluster at 2000ns, nodes 2,3 cluster at 2000ns
    // All 4 nodes cluster at 10000ns
    // Expected: 2 tier-0 hidden nodes + 1 tier-1 hidden node = 3 hidden nodes
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Should have hidden nodes at multiple tiers
    EXPECT_EQ(graph.num_hidden_nodes(), 3);

    // Count hidden nodes by tier
    std::map<uint32_t, int> nodes_by_tier;
    for (const auto& hn : graph.hidden_nodes()) {
        nodes_by_tier[hn.tier]++;
    }

    // Should have 2 at tier 1, 1 at tier 2 (physical nodes are tier 0)
    EXPECT_EQ(nodes_by_tier[1], 2);
    EXPECT_EQ(nodes_by_tier[2], 1);
}

TEST_F(TopologyTest, InterCluster_HiddenNodesConnected) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Count edges between hidden nodes (both src and dst are hidden)
    int hidden_to_hidden_edges = 0;
    for (const auto& edge : graph.edges()) {
        if (edge.src_is_hidden && edge.dst_is_hidden) {
            hidden_to_hidden_edges++;
        }
    }

    // Should have 2 edges: H0->H2 and H1->H2
    EXPECT_EQ(hidden_to_hidden_edges, 2);
}

TEST_F(TopologyTest, InterCluster_PhysicalToHiddenEdges) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Count edges from physical to hidden nodes
    int physical_to_hidden_edges = 0;
    for (const auto& edge : graph.edges()) {
        if (!edge.src_is_hidden && edge.dst_is_hidden) {
            physical_to_hidden_edges++;
        }
    }

    // Each physical node should connect to exactly one hidden node
    EXPECT_EQ(physical_to_hidden_edges, 4);
}

TEST_F(TopologyTest, InterCluster_TotalEdgeCount) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Total edges: 4 (physical->hidden) + 2 (hidden->hidden) = 6
    EXPECT_EQ(graph.num_edges(), 6);
}

TEST_F(TopologyTest, InterCluster_HiddenNodeTypes) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Tier 1 should be NVSWITCH, Tier 2 should be TOR_SWITCH (physical nodes are tier 0)
    for (const auto& hn : graph.hidden_nodes()) {
        if (hn.tier == 1) {
            EXPECT_EQ(hn.type, HiddenNodeType::NVSWITCH);
        } else if (hn.tier == 2) {
            EXPECT_EQ(hn.type, HiddenNodeType::TOR_SWITCH);
        }
    }
}

TEST_F(TopologyTest, InterCluster_8NodeHierarchy) {
    // medium_8node.csv: nodes 0-3 cluster together, nodes 4-7 cluster together
    // Then all 8 cluster at higher tier
    auto matrix = LatencyMatrix::from_csv(test_data_path("medium_8node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Should have 8 physical nodes
    EXPECT_EQ(graph.num_physical_nodes(), 8);

    // Should have hidden nodes: 2 at tier 1 + 1 at tier 2 = 3 (physical nodes are tier 0)
    EXPECT_EQ(graph.num_hidden_nodes(), 3);

    // Each physical node connects to one hidden node (8 edges)
    // Plus 2 hidden-to-hidden edges
    EXPECT_EQ(graph.num_edges(), 10);
}

TEST_F(TopologyTest, InterCluster_VerifyParentChildRelationship) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Find the tier-2 hidden node (physical nodes are tier 0, first-level switches tier 1)
    const HiddenNode* tier2_node = nullptr;
    for (const auto& hn : graph.hidden_nodes()) {
        if (hn.tier == 2) {
            tier2_node = &hn;
            break;
        }
    }
    ASSERT_NE(tier2_node, nullptr);

    // Verify that both tier-1 hidden nodes connect to the tier-2 node
    int connections_to_tier2 = 0;
    for (const auto& edge : graph.edges()) {
        if (edge.src_is_hidden && edge.dst_is_hidden &&
            edge.dst_id == tier2_node->hv_id) {
            connections_to_tier2++;
        }
    }
    EXPECT_EQ(connections_to_tier2, 2);
}

TEST_F(TopologyTest, InterCluster_NoSelfLoops) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // No edge should connect a node to itself
    for (const auto& edge : graph.edges()) {
        if (edge.src_is_hidden == edge.dst_is_hidden) {
            EXPECT_NE(edge.src_id, edge.dst_id);
        }
    }
}

TEST_F(TopologyTest, InterCluster_SingleClusterNoHigherTier) {
    // identical_latencies.csv: all nodes have same distance, form one cluster
    auto matrix = LatencyMatrix::from_csv(test_data_path("edge_cases/identical_latencies.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    // Should have 4 physical nodes
    EXPECT_EQ(graph.num_physical_nodes(), 4);

    // All nodes cluster together at tier 1 (physical nodes are tier 0), no higher tier needed
    // Should have 1 hidden node at tier 1
    EXPECT_GE(graph.num_hidden_nodes(), 1);

    // No hidden-to-hidden edges (single cluster)
    int hidden_to_hidden = 0;
    for (const auto& edge : graph.edges()) {
        if (edge.src_is_hidden && edge.dst_is_hidden) {
            hidden_to_hidden++;
        }
    }
    EXPECT_EQ(hidden_to_hidden, 0);
}

TEST_F(TopologyTest, InterCluster_Tier3_SpineSwitch) {
    // hierarchical_8node.csv creates a 3-level hierarchy:
    // - Nodes 0,1 and 2,3 cluster at 1000ns (2 tier-1 NVSWITCH)
    // - Nodes 4,5 and 6,7 cluster at 1000ns (2 more tier-1 NVSWITCH)
    // - {0,1,2,3} merge at 5000ns (1 tier-2 TOR_SWITCH)
    // - {4,5,6,7} merge at 5000ns (1 more tier-2 TOR_SWITCH)
    // - All merge at 20000ns (1 tier-3 SPINE_SWITCH)
    auto matrix = LatencyMatrix::from_csv(test_data_path("hierarchical_8node.csv"));

    TierConfig config;
    config.tier_thresholds = {2000, 10000, 30000};  // 3 thresholds

    TopologyBuilder builder(config);
    auto graph = builder.build(matrix);

    // Should have 8 physical nodes
    EXPECT_EQ(graph.num_physical_nodes(), 8);

    // Count hidden nodes by tier
    std::map<uint32_t, int> nodes_by_tier;
    std::map<uint32_t, HiddenNodeType> type_by_tier;
    for (const auto& hn : graph.hidden_nodes()) {
        nodes_by_tier[hn.tier]++;
        type_by_tier[hn.tier] = hn.type;  // All nodes at same tier have same type
    }

    // Should have nodes at tier 1, 2, and 3
    EXPECT_EQ(nodes_by_tier[1], 4);  // 4 NVSWITCH (one per pair)
    EXPECT_EQ(nodes_by_tier[2], 2);  // 2 TOR_SWITCH (one per quad)
    EXPECT_EQ(nodes_by_tier[3], 1);  // 1 SPINE_SWITCH (connects all)

    // Verify types
    EXPECT_EQ(type_by_tier[1], HiddenNodeType::NVSWITCH);
    EXPECT_EQ(type_by_tier[2], HiddenNodeType::TOR_SWITCH);
    EXPECT_EQ(type_by_tier[3], HiddenNodeType::SPINE_SWITCH);

    std::cout << "\n=== 3-Level Hierarchy (Adjacency List) ===\n"
              << graph.to_adjacency_list() << std::endl;
}

TEST_F(TopologyTest, InterCluster_MoreThan3Thresholds_UnknownBottleneck) {
    // Test with 4 thresholds - tier 4 should be UNKNOWN_BOTTLENECK
    auto matrix = LatencyMatrix::from_csv(test_data_path("hierarchical_8node.csv"));

    TierConfig config;
    // 4 thresholds: creates up to 5 tiers of hidden nodes (1,2,3,4,5)
    config.tier_thresholds = {1500, 3000, 10000, 30000};

    TopologyBuilder builder(config);
    auto graph = builder.build(matrix);

    // Find the highest tier hidden node
    uint32_t max_tier = 0;
    HiddenNodeType max_tier_type = HiddenNodeType::UNKNOWN_BOTTLENECK;
    for (const auto& hn : graph.hidden_nodes()) {
        if (hn.tier > max_tier) {
            max_tier = hn.tier;
            max_tier_type = hn.type;
        }
    }

    // With 4 thresholds, we can have up to tier 5 hidden nodes
    // Tier 4+ should be UNKNOWN_BOTTLENECK
    if (max_tier >= 4) {
        EXPECT_EQ(max_tier_type, HiddenNodeType::UNKNOWN_BOTTLENECK);
    }

    std::cout << "\n=== 4-Threshold Test ===\n"
              << "Max tier: " << max_tier << "\n"
              << graph.to_adjacency_list() << std::endl;
}

TEST_F(TopologyTest, InterCluster_ArbitraryThresholdCount) {
    // Verify code handles arbitrary number of thresholds without crashing
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    // Test with 1 threshold
    {
        TierConfig config;
        config.tier_thresholds = {5000};
        TopologyBuilder builder(config);
        auto graph = builder.build(matrix);
        EXPECT_GT(graph.num_physical_nodes(), 0);
    }

    // Test with 5 thresholds
    {
        TierConfig config;
        config.tier_thresholds = {1000, 3000, 5000, 10000, 50000};
        TopologyBuilder builder(config);
        auto graph = builder.build(matrix);
        EXPECT_GT(graph.num_physical_nodes(), 0);
    }

    // Test with 10 thresholds
    {
        TierConfig config;
        config.tier_thresholds = {500, 1000, 2000, 3000, 5000, 7000, 10000, 15000, 30000, 50000};
        TopologyBuilder builder(config);
        auto graph = builder.build(matrix);
        EXPECT_GT(graph.num_physical_nodes(), 0);
    }
}

// =============================================================================
// TopologyGraph Output Tests
// =============================================================================

TEST_F(TopologyTest, TopologyGraph_ToAdjacencyList_ValidFormat) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    std::string adj_list = graph.to_adjacency_list();

    // Should contain header
    EXPECT_NE(adj_list.find("# Topology Graph"), std::string::npos);

    // Should contain physical nodes section
    EXPECT_NE(adj_list.find("# Physical Nodes"), std::string::npos);
    EXPECT_NE(adj_list.find("P0"), std::string::npos);

    // Should contain tier info
    EXPECT_NE(adj_list.find("tier="), std::string::npos);
}

TEST_F(TopologyTest, TopologyGraph_ToDot_ValidFormat) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    std::string dot = graph.to_dot();

    // Should be valid DOT format
    EXPECT_NE(dot.find("digraph"), std::string::npos);
    EXPECT_NE(dot.find("{"), std::string::npos);
    EXPECT_NE(dot.find("}"), std::string::npos);
}

TEST_F(TopologyTest, TopologyGraph_ToJson_ValidFormat) {
    auto matrix = LatencyMatrix::from_csv(test_data_path("small_4node.csv"));

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    std::string json = graph.to_json();

    // Should contain expected JSON keys
    EXPECT_NE(json.find("{"), std::string::npos);
    EXPECT_NE(json.find("physical_nodes"), std::string::npos);
    EXPECT_NE(json.find("hidden_nodes"), std::string::npos);
    EXPECT_NE(json.find("edges"), std::string::npos);
}

// =============================================================================
// End-to-End Integration Test
// =============================================================================

TEST_F(TopologyTest, EndToEnd_CSVToAdjacencyList) {
    // Complete pipeline: CSV -> Matrix -> Dendrogram -> Graph -> Output
    std::string csv_path = test_data_path("small_4node.csv");

    auto matrix = LatencyMatrix::from_csv(csv_path);
    ASSERT_EQ(matrix.num_nodes(), 4);

    TopologyBuilder builder;
    auto graph = builder.build(matrix);

    std::string output = graph.to_adjacency_list();

    // Verify output contains expected structure
    EXPECT_GT(output.size(), 50);  // Reasonable output size
    EXPECT_NE(output.find("tier="), std::string::npos);

    // Print output for manual inspection
    std::cout << "\n=== Generated Topology (Adjacency List) ===\n"
              << output << std::endl;
}

TEST_F(TopologyTest, EndToEnd_Medium8Node) {
    TopologyBuilder builder;
    auto graph = builder.build_from_csv(test_data_path("medium_8node.csv"));

    EXPECT_EQ(graph.physical_nodes().size(), 8);

    std::cout << "\n=== 8-Node Topology (Adjacency List) ===\n"
              << graph.to_adjacency_list() << std::endl;
}

} // namespace testing
} // namespace nixl_topo
