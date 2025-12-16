#include "tier_config.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace nixl_topo {

TierConfig TierConfig::from_json(const std::string& path) {
    TierConfig config;

    if (path.empty()) {
        return config;  // Return defaults
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open config file: " << path
                  << ", using defaults\n";
        return config;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("tier_thresholds_ns")) {
            config.tier_thresholds = j["tier_thresholds_ns"].get<std::vector<uint64_t>>();
        }

        if (j.contains("linkage")) {
            config.linkage = linkage_from_string(j["linkage"].get<std::string>());
        }

        if (j.contains("min_cluster_size_for_switch")) {
            config.min_cluster_size_for_switch = j["min_cluster_size_for_switch"].get<uint32_t>();
        }

        if (j.contains("min_confidence_threshold")) {
            config.min_confidence_threshold = j["min_confidence_threshold"].get<double>();
        }

    } catch (const nlohmann::json::exception& e) {
        std::cerr << "Warning: Error parsing config file: " << e.what()
                  << ", using defaults\n";
        return TierConfig{};
    }

    return config;
}

void TierConfig::to_json(const std::string& path) const {
    nlohmann::json j;

    j["tier_thresholds_ns"] = tier_thresholds;
    j["linkage"] = to_string(linkage);
    j["min_cluster_size_for_switch"] = min_cluster_size_for_switch;
    j["min_confidence_threshold"] = min_confidence_threshold;

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Could not write config file: " << path << "\n";
        return;
    }

    file << j.dump(2) << "\n";
}

} // namespace nixl_topo
