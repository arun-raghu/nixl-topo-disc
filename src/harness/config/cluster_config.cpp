// =============================================================================
// cluster_config.cpp
// Implementation of cluster configuration parsing
// =============================================================================

#include "cluster_config.hpp"

#include <fstream>
#include <stdexcept>
#include <sstream>

#ifdef HAVE_JSON
#include <nlohmann/json.hpp>
#endif

namespace harness {

ClusterConfig ClusterConfig::from_json(const std::string& filepath) {
    ClusterConfig config;

#ifdef HAVE_JSON
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error in " + filepath + ": " + e.what());
    }

    // Required: num_agents
    if (!j.contains("num_agents")) {
        throw std::runtime_error("Config missing required field: num_agents");
    }
    config.num_agents = j["num_agents"].get<uint32_t>();

    // Optional fields with defaults
    if (j.contains("name")) {
        config.name = j["name"].get<std::string>();
    } else {
        config.name = "cluster";
    }

    if (j.contains("image")) {
        config.image = j["image"].get<std::string>();
    } else {
        config.image = DEFAULT_IMAGE;
    }

    // Network overrides (optional)
    if (j.contains("network_name")) {
        config.network_name = j["network_name"].get<std::string>();
    }
    if (j.contains("network_subnet")) {
        config.network_subnet = j["network_subnet"].get<std::string>();
    }
    if (j.contains("controller_ip")) {
        config.controller_ip = j["controller_ip"].get<std::string>();
    }

    // Network shaping configuration (optional)
    if (j.contains("network_shaping")) {
        config.network_shaping = NetworkShapingConfig::from_json(j["network_shaping"]);
    }

#else
    throw std::runtime_error("JSON support not enabled. Rebuild with -DWITH_JSON=ON");
#endif

    return config;
}

std::string ClusterConfig::get_agent_ip(uint32_t agent_id) const {
    // Default: 172.30.0.{10 + agent_id}
    std::ostringstream oss;
    oss << "172.30.0." << (DEFAULT_AGENT_IP_BASE + agent_id);
    return oss.str();
}

std::string ClusterConfig::get_agent_container_name(uint32_t agent_id) const {
    return std::string(DEFAULT_AGENT_NAME_PREFIX) + std::to_string(agent_id);
}

std::map<uint32_t, std::string> ClusterConfig::get_all_agent_ips() const {
    std::map<uint32_t, std::string> ips;
    for (uint32_t i = 0; i < num_agents; i++) {
        ips[i] = get_agent_ip(i);
    }
    return ips;
}

}  // namespace harness
