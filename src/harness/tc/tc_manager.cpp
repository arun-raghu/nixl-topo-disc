// =============================================================================
// tc_manager.cpp
// Traffic control manager implementation
// =============================================================================

#include "tc_manager.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>

namespace harness {

// =============================================================================
// NetworkShapingConfig implementation
// =============================================================================

#ifdef HAVE_JSON
NetworkShapingConfig NetworkShapingConfig::from_json(const nlohmann::json& j) {
    NetworkShapingConfig config;

    if (j.contains("enabled")) {
        config.enabled = j["enabled"].get<bool>();
    }

    if (j.contains("tiers")) {
        for (const auto& tier_json : j["tiers"]) {
            TierConfig tier;
            tier.name = tier_json.value("name", "unnamed");
            if (tier_json.contains("agents")) {
                tier.agents = tier_json["agents"].get<std::vector<uint32_t>>();
            }
            tier.intra_tier_latency_us = tier_json.value("intra_tier_latency_us", 100u);
            config.tiers.push_back(std::move(tier));
        }
    }

    if (j.contains("inter_tier")) {
        const auto& inter = j["inter_tier"];
        config.inter_tier.latency_us = inter.value("latency_us", 5000u);
        config.inter_tier.jitter_us = inter.value("jitter_us", 0u);
    }

    config.default_latency_us = j.value("default_latency_us", 1000u);

    return config;
}
#endif

int NetworkShapingConfig::get_tier_index(uint32_t agent_id) const {
    for (size_t i = 0; i < tiers.size(); i++) {
        for (uint32_t id : tiers[i].agents) {
            if (id == agent_id) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

bool NetworkShapingConfig::same_tier(uint32_t agent_a, uint32_t agent_b) const {
    int tier_a = get_tier_index(agent_a);
    int tier_b = get_tier_index(agent_b);
    return tier_a >= 0 && tier_a == tier_b;
}

void NetworkShapingConfig::print() const {
    std::cout << "Network Shaping Configuration:\n"
              << "  enabled: " << (enabled ? "true" : "false") << "\n"
              << "  tiers: " << tiers.size() << "\n";

    for (const auto& tier : tiers) {
        std::cout << "    - " << tier.name << ": agents=[";
        for (size_t i = 0; i < tier.agents.size(); i++) {
            if (i > 0) std::cout << ",";
            std::cout << tier.agents[i];
        }
        std::cout << "], latency=" << tier.intra_tier_latency_us << "us\n";
    }

    std::cout << "  inter_tier: latency=" << inter_tier.latency_us << "us";
    if (inter_tier.jitter_us > 0) {
        std::cout << " +/-" << inter_tier.jitter_us << "us";
    }
    std::cout << "\n";
    std::cout << "  default_latency: " << default_latency_us << "us\n";
}

// =============================================================================
// TcManager implementation
// =============================================================================

int TcManager::exec_tc(const std::string& container_name, const std::string& tc_args) {
    std::string cmd = "docker exec " + container_name + " tc " + tc_args + " 2>&1";

    std::array<char, 256> buffer;
    std::string output;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);

    if (!pipe) {
        return -1;
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get())) {
        output += buffer.data();
    }

    int status = pclose(pipe.release());
    int rc = WEXITSTATUS(status);

    if (rc != 0 && tc_args.find("qdisc del") == std::string::npos) {
        // Don't log errors for qdisc del (may not exist)
        std::cerr << "tc command failed: " << tc_args << "\n";
        if (!output.empty()) {
            std::cerr << "  output: " << output;
        }
    }

    return rc;
}

bool TcManager::tc_available(const std::string& container_name) {
    std::string cmd = "docker exec " + container_name + " which tc 2>/dev/null";
    return std::system(cmd.c_str()) == 0;
}

void TcManager::show_rules(const std::string& container_name, const std::string& interface) {
    std::cout << "=== tc rules for " << container_name << " ===\n";

    std::string cmd = "docker exec " + container_name + " tc qdisc show dev " + interface;
    std::system(cmd.c_str());

    cmd = "docker exec " + container_name + " tc class show dev " + interface;
    std::system(cmd.c_str());

    cmd = "docker exec " + container_name + " tc filter show dev " + interface;
    std::system(cmd.c_str());
}

std::vector<std::string> TcManager::build_tc_commands(
    uint32_t agent_id,
    const NetworkShapingConfig& config,
    const std::map<uint32_t, std::string>& agent_ips,
    const std::string& interface)
{
    std::vector<std::string> commands;
    int my_tier = config.get_tier_index(agent_id);

    // Get intra-tier latency (if agent is in a tier)
    uint32_t intra_latency_us = config.default_latency_us;
    if (my_tier >= 0) {
        intra_latency_us = config.tiers[my_tier].intra_tier_latency_us;
    }

    // Inter-tier config
    uint32_t inter_latency_us = config.inter_tier.latency_us;
    uint32_t inter_jitter_us = config.inter_tier.jitter_us;

    // Build latency strings for netem
    auto format_latency = [](uint32_t latency_us) -> std::string {
        if (latency_us >= 1000) {
            return std::to_string(latency_us / 1000) + "ms";
        }
        return std::to_string(latency_us) + "us";
    };

    std::string intra_delay = format_latency(intra_latency_us);
    std::string inter_delay = format_latency(inter_latency_us);
    if (inter_jitter_us > 0) {
        inter_delay += " " + format_latency(inter_jitter_us);
    }
    std::string default_delay = format_latency(config.default_latency_us);

    // Step 1: Clear existing rules (ignore errors)
    commands.push_back("qdisc del dev " + interface + " root");

    // Step 2: Add HTB root qdisc
    commands.push_back("qdisc add dev " + interface + " root handle 1: htb default 30");

    // Step 3: Add root class
    commands.push_back("class add dev " + interface + " parent 1: classid 1:1 htb rate 100gbit");

    // Step 4: Intra-tier class (1:10) - low latency
    commands.push_back("class add dev " + interface + " parent 1:1 classid 1:10 htb rate 100gbit");
    commands.push_back("qdisc add dev " + interface + " parent 1:10 handle 10: netem delay " + intra_delay);

    // Step 5: Inter-tier class (1:20) - high latency
    commands.push_back("class add dev " + interface + " parent 1:1 classid 1:20 htb rate 100gbit");
    commands.push_back("qdisc add dev " + interface + " parent 1:20 handle 20: netem delay " + inter_delay);

    // Step 6: Default class (1:30)
    commands.push_back("class add dev " + interface + " parent 1:1 classid 1:30 htb rate 100gbit");
    commands.push_back("qdisc add dev " + interface + " parent 1:30 handle 30: netem delay " + default_delay);

    // Step 7: Add filters for each destination agent
    for (const auto& [other_id, other_ip] : agent_ips) {
        if (other_id == agent_id) {
            continue;  // Skip self
        }

        std::string flowid;
        if (config.same_tier(agent_id, other_id)) {
            flowid = "1:10";  // Intra-tier
        } else {
            flowid = "1:20";  // Inter-tier
        }

        commands.push_back("filter add dev " + interface +
                          " protocol ip parent 1:0 prio 1 u32 match ip dst " +
                          other_ip + "/32 flowid " + flowid);
    }

    return commands;
}

bool TcManager::apply_rules(
    const std::string& container_name,
    uint32_t agent_id,
    const NetworkShapingConfig& config,
    const std::map<uint32_t, std::string>& agent_ips,
    const std::string& interface)
{
    if (!config.enabled) {
        return true;  // Nothing to do
    }

    auto commands = build_tc_commands(agent_id, config, agent_ips, interface);

    for (const auto& cmd : commands) {
        int rc = exec_tc(container_name, cmd);
        // Ignore errors on qdisc del (may not exist)
        if (rc != 0 && cmd.find("qdisc del") == std::string::npos) {
            std::cerr << "Failed to apply tc rule to " << container_name << ": " << cmd << "\n";
            return false;
        }
    }

    int my_tier = config.get_tier_index(agent_id);
    std::string tier_name = (my_tier >= 0) ? config.tiers[my_tier].name : "none";
    std::cout << "Applied tc rules to " << container_name
              << " (tier: " << tier_name << ")\n";

    return true;
}

bool TcManager::clear_rules(const std::string& container_name, const std::string& interface) {
    int rc = exec_tc(container_name, "qdisc del dev " + interface + " root");
    return rc == 0;
}

}  // namespace harness
