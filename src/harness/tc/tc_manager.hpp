// =============================================================================
// tc_manager.hpp
// Traffic control manager for network latency shaping
// =============================================================================

#ifndef HARNESS_TC_MANAGER_HPP
#define HARNESS_TC_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <cstdint>

#ifdef HAVE_JSON
#include <nlohmann/json.hpp>
#endif

namespace harness {

/// Configuration for a single network tier (rack/pod)
struct TierConfig {
    std::string name;
    std::vector<uint32_t> agents;         // Agent IDs in this tier
    uint32_t intra_tier_latency_us = 100; // Latency within tier (microseconds)
};

/// Configuration for inter-tier traffic
struct InterTierConfig {
    uint32_t latency_us = 5000;  // Cross-tier latency (microseconds)
    uint32_t jitter_us = 0;      // Latency variance (microseconds)
};

/// Full network shaping configuration
struct NetworkShapingConfig {
    bool enabled = false;
    std::vector<TierConfig> tiers;
    InterTierConfig inter_tier;
    uint32_t default_latency_us = 1000;  // Latency for unassigned agents

#ifdef HAVE_JSON
    /// Load from JSON object
    static NetworkShapingConfig from_json(const nlohmann::json& j);
#endif

    /// Get the tier index for an agent (-1 if not in any tier)
    int get_tier_index(uint32_t agent_id) const;

    /// Check if two agents are in the same tier
    bool same_tier(uint32_t agent_a, uint32_t agent_b) const;

    /// Print configuration summary
    void print() const;
};

/// Manages tc (traffic control) rules for network shaping
class TcManager {
public:
    /**
     * Apply tc rules to a container for network shaping
     * @param container_name Name of the Docker container
     * @param agent_id Agent ID for this container
     * @param config Network shaping configuration
     * @param agent_ips Map of agent_id -> IP address
     * @param interface Network interface name (default: eth0)
     * @return true if rules applied successfully
     */
    static bool apply_rules(
        const std::string& container_name,
        uint32_t agent_id,
        const NetworkShapingConfig& config,
        const std::map<uint32_t, std::string>& agent_ips,
        const std::string& interface = "eth0"
    );

    /**
     * Remove tc rules from a container
     * @param container_name Container name
     * @param interface Network interface (default: eth0)
     * @return true if rules removed successfully
     */
    static bool clear_rules(
        const std::string& container_name,
        const std::string& interface = "eth0"
    );

    /**
     * Check if tc is available in the container
     * @param container_name Container name
     * @return true if tc command is available
     */
    static bool tc_available(const std::string& container_name);

    /**
     * Show current tc rules for a container (for debugging)
     * @param container_name Container name
     * @param interface Network interface
     */
    static void show_rules(
        const std::string& container_name,
        const std::string& interface = "eth0"
    );

private:
    /**
     * Execute a tc command inside the container
     * @param container_name Container name
     * @param tc_args Arguments to pass to tc command
     * @return Exit code (0 = success)
     */
    static int exec_tc(
        const std::string& container_name,
        const std::string& tc_args
    );

    /**
     * Build tc commands for a specific agent
     * @param agent_id This agent's ID
     * @param config Network shaping configuration
     * @param agent_ips Map of agent_id -> IP address
     * @param interface Network interface
     * @return Vector of tc command arguments to execute in sequence
     */
    static std::vector<std::string> build_tc_commands(
        uint32_t agent_id,
        const NetworkShapingConfig& config,
        const std::map<uint32_t, std::string>& agent_ips,
        const std::string& interface
    );
};

}  // namespace harness

#endif  // HARNESS_TC_MANAGER_HPP
