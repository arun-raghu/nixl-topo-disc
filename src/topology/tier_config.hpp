#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nixl_topo {

// =============================================================================
// Linkage Type for Hierarchical Clustering
// =============================================================================

enum class LinkageType : uint8_t {
    SINGLE = 0,     // Minimum distance between clusters
    COMPLETE = 1,   // Maximum distance between clusters
    AVERAGE = 2     // Average distance between clusters
};

// =============================================================================
// Node Type Enumerations
// =============================================================================

enum class NodeType : uint8_t {
    GPU = 0,
    CPU = 1,
    NIC = 2,
    UNKNOWN = 255
};

enum class HiddenNodeType : uint8_t {
    TIER0_SWITCH = 0,
    TOR_SWITCH = 1,
    SPINE_SWITCH = 2,
    SHARED_LINK = 3,
    UNKNOWN_BOTTLENECK = 255
};

enum class EdgeType : uint8_t {
    DIRECT = 0,
    SWITCHED = 1,
    SHARED = 2
};

// =============================================================================
// Tier Configuration
// =============================================================================

struct TierConfig {
    // Latency thresholds in nanoseconds for tier boundaries
    // Nodes clustering below threshold[i] are assigned tier i
    std::vector<uint64_t> tier_thresholds = {
        5000,   // < 5us: Tier 0 (intra-node, NVLink/PCIe)
        15000,  // < 15us: Tier 1 (intra-rack, ToR switch)
        50000   // < 50us: Tier 2 (inter-rack, spine)
    };
    // Nodes clustering above all thresholds are assigned tier = thresholds.size()

    // Clustering algorithm parameters
    LinkageType linkage = LinkageType::SINGLE;

    // Hidden node inference parameters
    uint32_t min_cluster_size_for_switch = 2;
    double min_confidence_threshold = 0.5;

    // Load configuration from JSON file
    // Returns default config if path is empty or file doesn't exist
    static TierConfig from_json(const std::string& path);

    // Save configuration to JSON file
    void to_json(const std::string& path) const;
};

// =============================================================================
// String Conversion Utilities
// =============================================================================

inline const char* to_string(NodeType type) {
    switch (type) {
        case NodeType::GPU: return "GPU";
        case NodeType::CPU: return "CPU";
        case NodeType::NIC: return "NIC";
        default: return "UNKNOWN";
    }
}

inline const char* to_string(HiddenNodeType type) {
    switch (type) {
        case HiddenNodeType::TIER0_SWITCH: return "TIER0_SWITCH";
        case HiddenNodeType::TOR_SWITCH: return "TOR_SWITCH";
        case HiddenNodeType::SPINE_SWITCH: return "SPINE_SWITCH";
        case HiddenNodeType::SHARED_LINK: return "SHARED_LINK";
        default: return "UNKNOWN_BOTTLENECK";
    }
}

inline const char* to_string(EdgeType type) {
    switch (type) {
        case EdgeType::DIRECT: return "DIRECT";
        case EdgeType::SWITCHED: return "SWITCHED";
        case EdgeType::SHARED: return "SHARED";
        default: return "UNKNOWN";
    }
}

inline const char* to_string(LinkageType type) {
    switch (type) {
        case LinkageType::SINGLE: return "single";
        case LinkageType::COMPLETE: return "complete";
        case LinkageType::AVERAGE: return "average";
        default: return "single";
    }
}

inline LinkageType linkage_from_string(const std::string& s) {
    if (s == "complete") return LinkageType::COMPLETE;
    if (s == "average") return LinkageType::AVERAGE;
    return LinkageType::SINGLE;
}

} // namespace nixl_topo
