// =============================================================================
// harness.cpp
// Test harness implementation
// =============================================================================

#include "harness.hpp"
#include "network/network_manager.hpp"
#include "tc/tc_manager.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <vector>

#ifdef HAVE_JSON
#include <nlohmann/json.hpp>
#endif

namespace harness {

Harness::Harness(const ClusterConfig& config) : config_(config) {}

int Harness::exec_command(const std::string& cmd, std::string& output) {
    std::array<char, 128> buffer;
    output.clear();

    std::string full_cmd = cmd + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);

    if (!pipe) {
        return -1;
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }

    return pclose(pipe.release());
}

bool Harness::container_running(const std::string& container_name) {
    std::string cmd = "docker inspect -f '{{.State.Running}}' " + container_name + " 2>/dev/null";
    std::string output;
    int rc = exec_command(cmd, output);

    return rc == 0 && output.find("true") != std::string::npos;
}

void Harness::clear_signal_files(const std::string& output_dir) {
    std::filesystem::path out_path(output_dir);
    if (!std::filesystem::exists(out_path)) {
        return;
    }

    // List of signal/temp files to clean up
    std::vector<std::string> signal_files = {
        ".tc_ready",
        ".default_test_config.json",
    };

    for (const auto& filename : signal_files) {
        std::filesystem::path signal_file = out_path / filename;
        if (std::filesystem::exists(signal_file)) {
            std::error_code ec;
            std::filesystem::remove(signal_file, ec);
            if (!ec) {
                std::cout << "Cleared signal file: " << signal_file << "\n";
            }
        }
    }
}

bool Harness::up(const std::string& output_dir, const std::string& test_config) {
    std::cout << "=== Starting cluster: " << config_.name << " ===\n";
    std::cout << "Agents: " << config_.num_agents << "\n";
    std::cout << "Image: " << config_.image << "\n";
    std::cout << "Network: " << config_.network_name << " (" << config_.network_subnet << ")\n";

    // Determine effective test config
    std::string effective_test_config = test_config;

    // If network shaping is enabled and no test config specified, use default
    if (config_.network_shaping.enabled && test_config.empty()) {
        effective_test_config = "/output/.default_test_config.json";
        std::cout << "Test config: (using default for network shaping)\n";
    } else if (!test_config.empty()) {
        std::cout << "Test config: " << test_config << "\n";
    }
    std::cout << "\n";

    // Create output directory first (needed for default config)
    std::filesystem::path out_path(output_dir);

    // Clear any stale signal files from previous runs
    clear_signal_files(output_dir);
    if (!std::filesystem::exists(out_path)) {
        std::filesystem::create_directories(out_path);
        std::cout << "Created output directory: " << output_dir << "\n";
    }

    // If using default test config, create it BEFORE validation
    if (config_.network_shaping.enabled && test_config.empty()) {
        std::filesystem::path default_config_path = out_path / ".default_test_config.json";
        std::ofstream ofs(default_config_path);
        if (ofs) {
            ofs << "{\n";
            ofs << "  \"ping_pong\": {\n";
            ofs << "    \"message_size\": 64,\n";
            ofs << "    \"iterations\": 100,\n";
            ofs << "    \"warmup_iterations\": 10,\n";
            ofs << "    \"result_timeout_sec\": 30\n";
            ofs << "  },\n";
            ofs << "  \"output\": {\n";
            ofs << "    \"csv_path\": \"/output/latency_matrix.csv\"\n";
            ofs << "  },\n";
            ofs << "  \"test_all_pairs\": true,\n";
            ofs << "  \"run_bandwidth_tests\": false\n";
            ofs << "}\n";
            ofs.close();
            std::cout << "Created default test config: " << default_config_path << "\n";
        } else {
            std::cerr << "Warning: Could not create default test config\n";
        }
    }

    // Validate timeout configuration before starting
    if (config_.network_shaping.enabled && !validate_timeout_config(effective_test_config)) {
        std::cerr << "Aborting due to timeout configuration issues.\n";
        std::cerr << "Fix the configuration or use --force to override.\n";
        return false;
    }

    // Get absolute path for volume mount
    std::string abs_output_dir = std::filesystem::absolute(out_path).string();

    // Create network
    if (!NetworkManager::create_network(config_.network_name, config_.network_subnet)) {
        std::cerr << "Failed to create network\n";
        return false;
    }

    // Start controller
    if (!start_controller(abs_output_dir, effective_test_config)) {
        std::cerr << "Failed to start controller\n";
        return false;
    }

    std::cout << "\n=== Cluster started ===\n";
    std::cout << "Controller spawning " << config_.num_agents << " agents...\n";
    std::cout << "Output will be written to: " << abs_output_dir << "\n";

    // Apply network shaping if configured
    if (config_.network_shaping.enabled) {
        // Give controller time to spawn agents
        std::cout << "\nWaiting for agents to spawn before applying network shaping...\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));

        if (!apply_network_shaping(abs_output_dir)) {
            std::cerr << "Warning: Network shaping failed, continuing without it\n";
        }
    }

    return true;
}

bool Harness::start_controller(const std::string& output_dir, const std::string& test_config) {
    std::ostringstream cmd;

    cmd << "docker run -d"
        << " --name " << DEFAULT_CONTROLLER_NAME
        << " --network " << config_.network_name
        << " --ip " << config_.controller_ip
        << " -e UCX_TLS=tcp,shm"
        << " -e HARNESS_MODE=1"
        << " -e HARNESS_NETWORK=" << config_.network_name
        << " -e HARNESS_IMAGE=" << config_.image;

    // If network shaping is enabled, tell controller to wait for tc_ready signal
    if (config_.network_shaping.enabled) {
        cmd << " -e HARNESS_TC_READY_PATH=/output/.tc_ready";
    }

    cmd << " -v /var/run/docker.sock:/var/run/docker.sock"
        << " -v " << output_dir << ":/output"
        << " " << config_.image
        << " controller -n " << config_.num_agents;

    // Add test config if provided
    if (!test_config.empty()) {
        cmd << " -c " << test_config;
    }

    std::string output;
    int rc = exec_command(cmd.str(), output);

    if (rc != 0) {
        std::cerr << "Failed to start controller: " << output << "\n";
        return false;
    }

    std::cout << "Started controller container: " << DEFAULT_CONTROLLER_NAME << "\n";
    return true;
}

bool Harness::wait_for_controller(int timeout_sec) {
    std::cout << "Waiting for controller to become ready...\n";

    for (int i = 0; i < timeout_sec; i++) {
        if (container_running(DEFAULT_CONTROLLER_NAME)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cerr << "Controller did not start within " << timeout_sec << " seconds\n";
    return false;
}

bool Harness::down(const std::string& output_dir) {
    std::cout << "=== Stopping cluster: " << config_.name << " ===\n";

    int removed = 0;

    // Collect all expected container names
    std::vector<std::string> containers_to_remove;
    containers_to_remove.push_back(DEFAULT_CONTROLLER_NAME);
    for (uint32_t i = 0; i < config_.num_agents; i++) {
        containers_to_remove.push_back(config_.get_agent_container_name(i));
    }

    // Remove each container (handles both running and stopped)
    for (const auto& name : containers_to_remove) {
        if (NetworkManager::remove_container(name)) {
            removed++;
        }
    }

    std::cout << "Removed " << removed << " container(s)\n";

    // Remove network
    if (!NetworkManager::remove_network(config_.network_name)) {
        std::cerr << "Warning: Failed to remove network\n";
    }

    // Clean up signal files
    clear_signal_files(output_dir);

    std::cout << "=== Cluster stopped ===\n";
    return true;
}

bool Harness::apply_network_shaping(const std::string& output_dir) {
    if (!config_.network_shaping.enabled) {
        return true;  // Nothing to do
    }

    std::cout << "\n=== Applying network shaping ===\n";
    config_.network_shaping.print();

    // Get all agent IPs
    auto agent_ips = config_.get_all_agent_ips();

    // Wait for all agent containers to be running
    std::cout << "Waiting for agent containers to start...\n";
    for (uint32_t i = 0; i < config_.num_agents; i++) {
        std::string name = config_.get_agent_container_name(i);
        int tries = 0;
        while (!container_running(name) && tries < 60) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            tries++;
        }
        if (!container_running(name)) {
            std::cerr << "Agent container " << name << " not running after 60s\n";
            return false;
        }
    }
    std::cout << "All " << config_.num_agents << " agent containers running.\n";

    // Check if tc is available in the first container
    std::string first_container = config_.get_agent_container_name(0);
    if (!TcManager::tc_available(first_container)) {
        std::cerr << "Warning: tc command not available in containers.\n";
        std::cerr << "Network shaping disabled. Install iproute2 in the container image.\n";
        return false;
    }

    // Apply tc rules to each agent
    int success_count = 0;
    for (uint32_t i = 0; i < config_.num_agents; i++) {
        std::string name = config_.get_agent_container_name(i);
        if (TcManager::apply_rules(name, i, config_.network_shaping, agent_ips)) {
            success_count++;
        } else {
            std::cerr << "Warning: Failed to apply tc rules to " << name << "\n";
        }
    }

    std::cout << "Network shaping applied to " << success_count << "/"
              << config_.num_agents << " agents\n";

    // Create tc_ready signal file to tell controller to start tests
    std::filesystem::path signal_file = std::filesystem::path(output_dir) / ".tc_ready";
    std::ofstream ofs(signal_file);
    if (ofs) {
        ofs << "tc rules applied\n";
        ofs.close();
        std::cout << "Created tc_ready signal: " << signal_file << "\n";
    } else {
        std::cerr << "Warning: Failed to create tc_ready signal file\n";
    }

    return success_count == static_cast<int>(config_.num_agents);
}

std::string Harness::collect_results(const std::string& output_dir) {
    std::cout << "=== Collecting results ===\n";

    // Wait for controller to finish
    std::cout << "Waiting for controller to complete...\n";
    int wait_count = 0;
    const int max_wait = 600;  // 10 minutes max
    while (container_running(DEFAULT_CONTROLLER_NAME) && wait_count < max_wait) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_count++;
        if (wait_count % 10 == 0) {
            std::cout << "  Still waiting... (" << wait_count << "s)\n";
        }
    }

    if (wait_count >= max_wait) {
        std::cerr << "Timeout waiting for controller to complete\n";
        return "";
    }

    std::cout << "Controller finished after " << wait_count << " seconds\n";

    // Create timestamped subdirectory
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time_t);

    std::ostringstream timestamp;
    timestamp << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");

    std::filesystem::path results_dir = std::filesystem::path(output_dir) / timestamp.str();
    std::filesystem::create_directories(results_dir);
    std::cout << "Created results directory: " << results_dir << "\n";

    // Copy results from output directory
    std::filesystem::path out_path(output_dir);
    int files_copied = 0;

    for (const auto& entry : std::filesystem::directory_iterator(out_path)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            // Skip if it's a config file we passed in
            if (filename.find("test_config") != std::string::npos) {
                continue;
            }
            // Copy result files (csv, json, etc.)
            if (filename.find(".csv") != std::string::npos ||
                filename.find(".json") != std::string::npos ||
                filename.find("result") != std::string::npos ||
                filename.find("latency") != std::string::npos ||
                filename.find("bandwidth") != std::string::npos) {
                std::filesystem::copy(entry.path(), results_dir / filename,
                                     std::filesystem::copy_options::overwrite_existing);
                std::cout << "  Copied: " << filename << "\n";
                files_copied++;
            }
        }
    }

    std::cout << "Copied " << files_copied << " result file(s)\n";

    // Run topology_viz on latency matrix if it exists
    std::filesystem::path latency_csv = results_dir / "latency_matrix.csv";
    if (std::filesystem::exists(latency_csv)) {
        std::filesystem::path svg_output = results_dir / "topology.svg";

        // Generate tier config from network shaping settings if enabled
        std::filesystem::path tier_config_path;
        if (config_.network_shaping.enabled) {
            tier_config_path = results_dir / "tier_config.json";
            generate_tier_config(tier_config_path.string());
        }

        // Try to find topology_viz in standard locations
        std::vector<std::string> viz_paths = {
            "topology_viz",  // In PATH
            "/usr/local/bin/topology_viz",
            "./topology_viz",
        };

        // Also try relative to the harness binary
        std::filesystem::path exe_path = std::filesystem::read_symlink("/proc/self/exe");
        viz_paths.push_back((exe_path.parent_path() / "topology_viz").string());

        std::string viz_cmd;
        for (const auto& path : viz_paths) {
            std::string test_cmd = "which " + path + " 2>/dev/null || test -x " + path;
            std::string dummy;
            if (exec_command(test_cmd, dummy) == 0 || std::filesystem::exists(path)) {
                viz_cmd = path + " " + latency_csv.string() + " -o " + svg_output.string();
                // Add tier config if we generated one
                if (!tier_config_path.empty()) {
                    viz_cmd += " -c " + tier_config_path.string();
                }
                break;
            }
        }

        if (viz_cmd.empty()) {
            std::cerr << "Warning: topology_viz not found, skipping SVG generation\n";
        } else {
            std::cout << "Generating topology visualization...\n";
            std::string output;
            int rc = exec_command(viz_cmd, output);
            if (rc == 0) {
                std::cout << "  Created: topology.svg\n";
            } else {
                std::cerr << "Warning: Failed to generate topology SVG: " << output << "\n";
            }
        }
    } else {
        std::cout << "No latency_matrix.csv found, skipping topology visualization\n";
    }

    std::cout << "=== Results collected in: " << results_dir << " ===\n";
    return results_dir.string();
}

void Harness::generate_tier_config(const std::string& config_path) const {
    if (!config_.network_shaping.enabled) {
        return;
    }

    // Find max intra-tier latency across all tiers
    uint32_t max_intra_tier_us = 0;
    for (const auto& tier : config_.network_shaping.tiers) {
        if (tier.intra_tier_latency_us > max_intra_tier_us) {
            max_intra_tier_us = tier.intra_tier_latency_us;
        }
    }

    uint32_t inter_tier_us = config_.network_shaping.inter_tier.latency_us;

    // Set tier threshold at geometric mean between intra and inter tier latencies
    // This provides good separation for the topology clustering
    uint64_t threshold_ns;
    if (inter_tier_us > 0 && max_intra_tier_us > 0) {
        // Geometric mean provides good log-scale separation
        double geom_mean_us = std::sqrt(static_cast<double>(max_intra_tier_us) *
                                        static_cast<double>(inter_tier_us));
        threshold_ns = static_cast<uint64_t>(geom_mean_us * 1000.0);
    } else if (inter_tier_us > 0) {
        // No intra-tier latency specified, use half of inter-tier
        threshold_ns = static_cast<uint64_t>(inter_tier_us) * 500;
    } else {
        // Default threshold
        threshold_ns = 1000000;  // 1ms
    }

    // Write tier config JSON
    std::ofstream ofs(config_path);
    if (!ofs) {
        std::cerr << "Warning: Failed to create tier config file: " << config_path << "\n";
        return;
    }

    ofs << "{\n";
    ofs << "  \"tier_thresholds_ns\": [" << threshold_ns << "],\n";
    ofs << "  \"linkage\": \"single\",\n";
    ofs << "  \"min_cluster_size_for_switch\": 2\n";
    ofs << "}\n";
    ofs.close();

    std::cout << "Generated tier config: " << config_path << "\n";
    std::cout << "  Threshold: " << threshold_ns / 1000 << " us "
              << "(separates " << max_intra_tier_us << "us intra-tier from "
              << inter_tier_us << "us inter-tier)\n";
}

void Harness::status() const {
    std::cout << "=== Cluster Status: " << config_.name << " ===\n\n";

    // Check network
    bool network_up = NetworkManager::network_exists(config_.network_name);
    std::cout << "Network: " << config_.network_name << " - "
              << (network_up ? "UP" : "DOWN") << "\n\n";

    if (!network_up) {
        std::cout << "Cluster is not running.\n";
        return;
    }

    // List containers
    auto containers = NetworkManager::list_containers_on_network(config_.network_name);
    std::cout << "Containers (" << containers.size() << "):\n";

    for (const auto& container : containers) {
        bool running = container_running(container);
        std::cout << "  " << container << " - " << (running ? "RUNNING" : "STOPPED") << "\n";
    }

    // Check expected containers
    std::cout << "\nExpected containers:\n";
    std::cout << "  Controller: " << DEFAULT_CONTROLLER_NAME << " - "
              << (container_running(DEFAULT_CONTROLLER_NAME) ? "RUNNING" : "NOT FOUND") << "\n";

    for (uint32_t i = 0; i < config_.num_agents; i++) {
        std::string agent_name = config_.get_agent_container_name(i);
        std::cout << "  Agent " << i << ": " << agent_name << " - "
                  << (container_running(agent_name) ? "RUNNING" : "NOT FOUND") << "\n";
    }
}

bool Harness::validate_timeout_config(const std::string& test_config_path) const {
#ifndef HAVE_JSON
    // Can't validate without JSON support
    return true;
#else
    if (!config_.network_shaping.enabled) {
        return true;  // No network shaping, no timeout concerns
    }

    // Hardcoded agent timeout values (from agent.cpp)
    constexpr uint32_t PING_PONG_TIMEOUT_SEC = 5;
    constexpr uint32_t BANDWIDTH_TIMEOUT_SEC = 30;

    // Default test parameters (from controller.hpp)
    uint32_t ping_pong_iterations = 100;
    uint32_t ping_pong_warmup = 10;
    uint32_t bandwidth_iterations = 100;
    uint32_t bandwidth_window = 64;
    bool run_bandwidth_tests = true;

    // Try to read test config if provided
    if (!test_config_path.empty()) {
        // Convert container path to host path
        std::string host_path = test_config_path;
        if (host_path.find("/output/") == 0) {
            host_path = "./output/" + host_path.substr(8);
        }

        std::ifstream file(host_path);
        if (file.is_open()) {
            try {
                nlohmann::json j;
                file >> j;

                if (j.contains("ping_pong")) {
                    auto& pp = j["ping_pong"];
                    if (pp.contains("iterations")) {
                        ping_pong_iterations = pp["iterations"].get<uint32_t>();
                    }
                    if (pp.contains("warmup_iterations")) {
                        ping_pong_warmup = pp["warmup_iterations"].get<uint32_t>();
                    }
                }

                if (j.contains("bandwidth")) {
                    auto& bw = j["bandwidth"];
                    if (bw.contains("iterations")) {
                        bandwidth_iterations = bw["iterations"].get<uint32_t>();
                    }
                    if (bw.contains("window_size")) {
                        bandwidth_window = bw["window_size"].get<uint32_t>();
                    }
                }

                if (j.contains("run_bandwidth_tests")) {
                    run_bandwidth_tests = j["run_bandwidth_tests"].get<bool>();
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not parse test config for validation: " << e.what() << "\n";
            }
        }
    }

    // Get max latency from network shaping config
    uint32_t max_latency_us = config_.network_shaping.inter_tier.latency_us;
    uint32_t jitter_us = config_.network_shaping.inter_tier.jitter_us;
    uint32_t worst_case_latency_us = max_latency_us + jitter_us;

    // Calculate expected test durations
    // Round-trip latency = 2 * one-way latency
    uint32_t rtt_us = worst_case_latency_us * 2;

    bool has_issues = false;

    std::cout << "\n=== Timeout Validation ===\n";
    std::cout << "Network shaping: inter-tier latency = " << max_latency_us << "us (+/-" << jitter_us << "us)\n";
    std::cout << "Worst-case RTT: " << rtt_us << "us (" << rtt_us / 1000.0 << "ms)\n\n";

    // Ping-pong validation
    {
        uint32_t total_iterations = ping_pong_iterations + ping_pong_warmup;
        double expected_time_sec = (total_iterations * rtt_us) / 1000000.0;
        double margin = PING_PONG_TIMEOUT_SEC - expected_time_sec;

        std::cout << "Ping-pong test:\n";
        std::cout << "  Iterations: " << ping_pong_iterations << " + " << ping_pong_warmup << " warmup = " << total_iterations << "\n";
        std::cout << "  Expected time with tc delay: " << expected_time_sec << "s\n";
        std::cout << "  Agent timeout: " << PING_PONG_TIMEOUT_SEC << "s\n";
        std::cout << "  Margin: " << margin << "s\n";

        if (margin < 0) {
            std::cerr << "  [ERROR] Ping-pong tests WILL timeout!\n";
            std::cerr << "  Reduce iterations or inter-tier latency.\n";
            has_issues = true;
        } else if (margin < 1.0) {
            std::cerr << "  [WARNING] Tight margin - ping-pong tests may timeout.\n";
        } else {
            std::cout << "  [OK] Sufficient margin.\n";
        }
        std::cout << "\n";
    }

    // Bandwidth validation (if enabled)
    if (run_bandwidth_tests) {
        // Bandwidth tests do window-based transfers
        // Each "iteration" involves: send window messages, wait for ACKs
        // Simplified model: each iteration ~= 2 RTTs (send + ack phases)
        uint32_t total_rtts = bandwidth_iterations * 2;
        double expected_time_sec = (total_rtts * rtt_us) / 1000000.0;
        double margin = BANDWIDTH_TIMEOUT_SEC - expected_time_sec;

        std::cout << "Bandwidth test:\n";
        std::cout << "  Iterations: " << bandwidth_iterations << ", window: " << bandwidth_window << "\n";
        std::cout << "  Expected time with tc delay: " << expected_time_sec << "s (estimated)\n";
        std::cout << "  Agent timeout: " << BANDWIDTH_TIMEOUT_SEC << "s\n";
        std::cout << "  Margin: " << margin << "s\n";

        if (margin < 0) {
            std::cerr << "  [ERROR] Bandwidth tests WILL timeout!\n";
            std::cerr << "  Set \"run_bandwidth_tests\": false in test config,\n";
            std::cerr << "  or reduce iterations/inter-tier latency.\n";
            has_issues = true;
        } else if (margin < 5.0) {
            std::cerr << "  [WARNING] Tight margin - bandwidth tests may timeout.\n";
        } else {
            std::cout << "  [OK] Sufficient margin.\n";
        }
        std::cout << "\n";
    } else {
        std::cout << "Bandwidth test: DISABLED (skipping validation)\n\n";
    }

    if (has_issues) {
        std::cerr << "=== Timeout issues detected! ===\n";
        std::cerr << "Tests will likely fail. Please adjust configuration.\n\n";
    } else {
        std::cout << "=== Timeout validation passed ===\n\n";
    }

    return !has_issues;
#endif
}

}  // namespace harness
