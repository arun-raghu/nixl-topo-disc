// =============================================================================
// cluster_config.hpp
// Configuration for test harness cluster definition
// =============================================================================

#ifndef HARNESS_CLUSTER_CONFIG_HPP
#define HARNESS_CLUSTER_CONFIG_HPP

#include <string>
#include <cstdint>

namespace harness {

// Default network configuration
constexpr const char* DEFAULT_NETWORK_NAME = "harness-net";
constexpr const char* DEFAULT_NETWORK_SUBNET = "172.30.0.0/16";
constexpr const char* DEFAULT_CONTROLLER_IP = "172.30.0.2";
constexpr uint8_t DEFAULT_AGENT_IP_BASE = 10;  // Agents start at 172.30.0.10

// Default container configuration
constexpr const char* DEFAULT_IMAGE = "nixl-topo-disc:latest";
constexpr const char* DEFAULT_CONTROLLER_NAME = "topo-controller";
constexpr const char* DEFAULT_AGENT_NAME_PREFIX = "topo-agent-";

/**
 * Cluster configuration loaded from JSON
 */
struct ClusterConfig {
    std::string name;              // Cluster name (for identification)
    uint32_t num_agents = 4;       // Number of agent containers
    std::string image;             // Docker image to use

    // Network settings (use defaults)
    std::string network_name = DEFAULT_NETWORK_NAME;
    std::string network_subnet = DEFAULT_NETWORK_SUBNET;
    std::string controller_ip = DEFAULT_CONTROLLER_IP;

    /**
     * Load configuration from JSON file
     * @param filepath Path to cluster.json
     * @return Parsed configuration
     * @throws std::runtime_error if file cannot be read or parsed
     */
    static ClusterConfig from_json(const std::string& filepath);

    /**
     * Compute IP address for agent
     * @param agent_id Agent index (0 to num_agents-1)
     * @return IP address string (e.g., "172.30.0.10")
     */
    std::string get_agent_ip(uint32_t agent_id) const;

    /**
     * Get container name for agent
     * @param agent_id Agent index
     * @return Container name (e.g., "topo-agent-0")
     */
    std::string get_agent_container_name(uint32_t agent_id) const;
};

}  // namespace harness

#endif  // HARNESS_CLUSTER_CONFIG_HPP
