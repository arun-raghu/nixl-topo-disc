#include "controller.hpp"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <vector>
#include <map>

namespace {

std::vector<pid_t> g_child_pids;
std::string g_agent_binary_path;

// Harness mode: spawn containers instead of processes
bool g_harness_mode = false;
std::string g_harness_network;
std::string g_harness_image;
std::string g_tc_ready_path;  // Path to tc_ready signal file

// Shutdown flag
volatile std::sig_atomic_t g_shutdown_requested = 0;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " -n <num_agents> [-t <timeout_sec>] [-a <agent_path>] [-c <config_file>]\n"
              << "\n"
              << "Options:\n"
              << "  -n <num_agents>   Number of agents to wait for (required)\n"
              << "  -t <timeout_sec>  Timeout in seconds for agent registration (default: 60)\n"
              << "  -a <agent_path>   Path to agent binary (default: ./agent in same dir as controller)\n"
              << "  -c <config_file>  Path to JSON config file for test parameters\n"
              << "  -h                Show this help message\n"
              << "\n"
              << "Config file format (JSON):\n"
              << "  {\n"
              << "    \"ping_pong\": {\n"
              << "      \"message_size\": 64,\n"
              << "      \"iterations\": 10,\n"
              << "      \"warmup_iterations\": 5,\n"
              << "      \"result_timeout_sec\": 30\n"
              << "    },\n"
              << "    \"bandwidth\": {\n"
              << "      \"message_sizes\": [1024, 4096, 16384, 65536, 262144, 1048576, 4194304],\n"
              << "      \"iterations\": 100,\n"
              << "      \"warmup_iterations\": 10,\n"
              << "      \"window_size\": 64\n"
              << "    },\n"
              << "    \"latency_sweep\": {\n"
              << "      \"enabled\": false,\n"
              << "      \"message_sizes\": [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536],\n"
              << "      \"iterations\": 1000,\n"
              << "      \"warmup_iterations\": 100\n"
              << "    },\n"
              << "    \"output\": {\n"
              << "      \"csv_path\": \"/tmp/latency_matrix.csv\",\n"
              << "      \"bandwidth_csv_path\": \"/tmp/bandwidth_matrix.csv\"\n"
              << "    },\n"
              << "    \"test_all_pairs\": true,\n"
              << "    \"run_bandwidth_tests\": true,\n"
              << "    \"run_latency_sweep\": false\n"
              << "  }\n";
}

// Get directory containing the executable
std::string get_exe_dir(const char* argv0) {
    // Try /proc/self/exe first (Linux-specific)
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string path(buf);
        size_t pos = path.rfind('/');
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
    }
    // Fallback: use argv[0]
    std::string path(argv0);
    size_t pos = path.rfind('/');
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
    return ".";
}

bool spawn_agent(uint32_t agent_id, const std::map<std::string, std::string>& env_vars) {
    std::string log_file = "/tmp/agent_" + std::to_string(agent_id) + ".log";

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Failed to fork for agent " << agent_id << "\n";
        return false;
    }

    if (pid == 0) {
        // Child process

        // Open log file for stdout/stderr
        int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd < 0) {
            std::cerr << "Failed to open log file: " << log_file << "\n";
            _exit(1);
        }

        // Redirect stdout and stderr to log file
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);

        // Set environment variables
        for (const auto& [key, value] : env_vars) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        // Execute agent binary
        execl(g_agent_binary_path.c_str(), "agent", nullptr);

        // If exec fails
        std::cerr << "Failed to exec agent: " << g_agent_binary_path << "\n";
        _exit(1);
    }

    // Parent process
    g_child_pids.push_back(pid);
    std::cout << "  Spawned agent " << agent_id << " (PID: " << pid << ") -> " << log_file << "\n";
    return true;
}

bool spawn_agent_container(uint32_t agent_id, const std::map<std::string, std::string>& env_vars) {
    // Compute agent IP: 172.30.0.{10 + agent_id}
    std::string agent_ip = "172.30.0." + std::to_string(10 + agent_id);
    std::string container_name = "topo-agent-" + std::to_string(agent_id);

    // Build docker run command
    std::string cmd = "docker run -d";
    cmd += " --name " + container_name;
    cmd += " --network " + g_harness_network;
    cmd += " --ip " + agent_ip;
    cmd += " --cap-add=NET_ADMIN";  // Required for tc network shaping

    // Add environment variables
    for (const auto& [key, value] : env_vars) {
        cmd += " -e " + key + "=" + value;
    }
    cmd += " -e UCX_TLS=tcp,shm";

    // Image and command
    cmd += " " + g_harness_image + " agent";

    // Execute docker run
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "Failed to spawn agent container " << agent_id << "\n";
        return false;
    }

    std::cout << "  Spawned agent " << agent_id << " container: " << container_name
              << " (" << agent_ip << ")\n";
    return true;
}

void terminate_agents() {
    for (pid_t pid : g_child_pids) {
        kill(pid, SIGTERM);
    }
    // Give them time to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (pid_t pid : g_child_pids) {
        int status;
        waitpid(pid, &status, WNOHANG);
    }
    g_child_pids.clear();
}

// Wait for tc rules to be applied (signal file from harness)
bool wait_for_tc_ready(int timeout_sec) {
    if (g_tc_ready_path.empty()) {
        return true;  // No wait needed
    }

    std::cout << "Waiting for network shaping to be applied...\n";
    std::cout << "  Signal file: " << g_tc_ready_path << "\n";

    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(timeout_sec);

    while (!g_shutdown_requested) {
        // Check if signal file exists
        if (access(g_tc_ready_path.c_str(), F_OK) == 0) {
            std::cout << "Network shaping ready!\n";
            return true;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            std::cerr << "Warning: Timeout waiting for tc_ready signal, proceeding anyway\n";
            return true;  // Continue anyway, tests will just run without shaping
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return false;
}

void signal_handler(int signal) {
    (void)signal;
    g_shutdown_requested = 1;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Disable stdout/stderr buffering for immediate log output
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    uint32_t num_agents = 0;
    uint32_t timeout_sec = 60;
    std::string agent_path;
    std::string config_path;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: -n requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
            num_agents = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: -t requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
            timeout_sec = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "-a") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: -a requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
            agent_path = argv[++i];
        } else if (std::strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: -c requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (num_agents == 0) {
        std::cerr << "Error: -n <num_agents> is required and must be > 0\n";
        print_usage(argv[0]);
        return 1;
    }

    // Check for harness mode (containerized deployment)
    const char* harness_mode_env = std::getenv("HARNESS_MODE");
    if (harness_mode_env && std::strcmp(harness_mode_env, "1") == 0) {
        g_harness_mode = true;

        const char* network_env = std::getenv("HARNESS_NETWORK");
        const char* image_env = std::getenv("HARNESS_IMAGE");

        if (!network_env || !image_env) {
            std::cerr << "Error: HARNESS_MODE=1 requires HARNESS_NETWORK and HARNESS_IMAGE\n";
            return 1;
        }

        g_harness_network = network_env;
        g_harness_image = image_env;

        // Check for tc ready signal path (optional)
        const char* tc_ready_env = std::getenv("HARNESS_TC_READY_PATH");
        if (tc_ready_env) {
            g_tc_ready_path = tc_ready_env;
        }

        std::cout << "Harness mode enabled\n";
        std::cout << "  Network: " << g_harness_network << "\n";
        std::cout << "  Image: " << g_harness_image << "\n";
        if (!g_tc_ready_path.empty()) {
            std::cout << "  TC ready path: " << g_tc_ready_path << "\n";
        }
    }

    // Determine agent binary path (only needed in non-harness mode)
    if (!g_harness_mode) {
        if (agent_path.empty()) {
            g_agent_binary_path = get_exe_dir(argv[0]) + "/agent";
        } else {
            g_agent_binary_path = agent_path;
        }

        // Verify agent binary exists
        if (access(g_agent_binary_path.c_str(), X_OK) != 0) {
            std::cerr << "Error: Agent binary not found or not executable: " << g_agent_binary_path << "\n";
            return 1;
        }
    }

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Load test configuration
    nixl_topo::TestConfig config;
    if (!config_path.empty()) {
        try {
            config = nixl_topo::TestConfig::from_json(config_path);
            std::cout << "Loaded config from: " << config_path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Error loading config: " << e.what() << "\n";
            return 1;
        }
    } else {
        std::cout << "No config file specified (-c), using defaults\n";
    }
    config.print();

    std::cout << "\nStarting controller for " << num_agents << " agents\n";
    std::cout << "Registration timeout: " << timeout_sec << " seconds\n";

    // Initialize controller
    nixl_topo::Controller controller;
    if (!controller.initialize(num_agents)) {
        std::cerr << "Error: Failed to initialize controller\n";
        return 1;
    }

    std::cout << "Controller initialized successfully\n";
    std::cout << "Controller PID: " << getpid() << "\n";
    std::cout << "Buffer address: 0x" << std::hex
              << reinterpret_cast<uintptr_t>(controller.buffer().data())
              << std::dec << "\n";
    std::cout << "Buffer size: " << controller.buffer().size() << " bytes\n";

    // Spawn agent processes or containers
    std::cout << "\nSpawning " << num_agents << " agents...\n";
    if (g_harness_mode) {
        std::cout << "Mode: containerized (harness)\n";
    } else {
        std::cout << "Mode: local processes\n";
        std::cout << "Agent binary: " << g_agent_binary_path << "\n";
    }

    for (uint32_t i = 0; i < num_agents; ++i) {
        auto env_vars = controller.get_agent_env_vars(i);
        bool success = g_harness_mode
            ? spawn_agent_container(i, env_vars)
            : spawn_agent(i, env_vars);

        if (!success) {
            std::cerr << "Error: Failed to spawn agent " << i << "\n";
            if (!g_harness_mode) {
                terminate_agents();
            }
            controller.shutdown();
            return 1;
        }
    }

    if (g_harness_mode) {
        std::cout << "All agent containers spawned.\n\n";
    } else {
        std::cout << "All agents spawned. Logs in /tmp/agent_*.log\n\n";
    }

    // Wait for all agents to register
    std::cout << "Waiting for agents to register...\n";

    auto timeout = std::chrono::seconds(timeout_sec);
    auto start_time = std::chrono::steady_clock::now();

    while (!g_shutdown_requested) {
        bool all_registered = controller.wait_for_all_agents(std::chrono::milliseconds(1000));

        if (all_registered) {
            std::cout << "All " << num_agents << " agents registered!\n";
            break;
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout) {
            std::cerr << "Error: Timeout waiting for agents to register\n";

            // Report which agents are missing
            for (uint32_t i = 0; i < num_agents; ++i) {
                if (!controller.buffer().is_agent_registered(i)) {
                    std::cerr << "  Agent " << i << ": not registered\n";
                }
            }

            if (!g_harness_mode) {
                terminate_agents();
            }
            controller.shutdown();
            return 1;
        }

        // Print status periodically
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        uint32_t registered_count = 0;
        for (uint32_t i = 0; i < num_agents; ++i) {
            if (controller.buffer().is_agent_registered(i)) {
                registered_count++;
            }
        }
        std::cout << "  [" << elapsed_sec << "s] " << registered_count << "/" << num_agents
                  << " agents registered\n";
    }

    if (g_shutdown_requested) {
        std::cout << "\nShutdown requested, exiting...\n";
        if (!g_harness_mode) {
            terminate_agents();
        }
        controller.shutdown();
        return 0;
    }

    // Signal rendezvous complete
    std::cout << "Signaling rendezvous complete to all agents...\n";
    controller.signal_rendezvous_complete();
    std::cout << "Rendezvous complete!\n";

    // Wait for all agents to complete peer discovery before sending commands
    if (!controller.wait_for_peer_discovery(std::chrono::seconds(timeout_sec))) {
        std::cerr << "Error: Timeout waiting for peer discovery\n";
        if (!g_harness_mode) {
            terminate_agents();
        }
        controller.shutdown();
        return 1;
    }

    // Wait for network shaping to be applied (if configured)
    if (!wait_for_tc_ready(timeout_sec)) {
        std::cerr << "Aborted while waiting for tc ready signal\n";
        if (!g_harness_mode) {
            terminate_agents();
        }
        controller.shutdown();
        return 1;
    }

    // Execute tests using config parameters
    if (!g_shutdown_requested && num_agents >= 2) {
        auto result_timeout = std::chrono::seconds(config.result_timeout_sec);
        uint32_t test_num = 0;

        // === Latency Tests ===
        std::cout << "\n=== Executing Latency Tests ===\n";

        // Test all unique pairs (i < j only, since ping-pong measures RTT)
        for (uint32_t i = 0; i < num_agents && !g_shutdown_requested; ++i) {
            for (uint32_t j = i + 1; j < num_agents && !g_shutdown_requested; ++j) {
                if (!controller.buffer().is_agent_registered(i) ||
                    !controller.buffer().is_agent_registered(j)) {
                    continue;  // Skip unregistered agents
                }

                ++test_num;
                std::cout << "\n--- Latency Test " << test_num << ": initiator=" << i
                          << " responder=" << j << " ---\n";

                if (controller.issue_ping_pong_test(i, j, config.message_size,
                                                     config.iterations, config.warmup_iterations)) {
                    std::cout << "Waiting for results...\n";
                    if (controller.wait_for_results({i}, controller.command_seq(), result_timeout)) {
                        std::cout << "Latency test " << test_num << " completed successfully!\n";
                        const auto* result = controller.get_result(i);
                        if (result) {
                            controller.store_test_result(i, j, *result);
                        }
                    } else {
                        std::cerr << "Error: Timeout waiting for latency test " << test_num << " results\n";
                    }
                } else {
                    std::cerr << "Error: Failed to issue latency test " << test_num << "\n";
                }
            }
        }

        std::cout << "\n=== Latency Tests Complete ===\n";

        // Write latency matrix to CSV file
        std::ofstream csv_file(config.output_csv_path);
        if (csv_file.is_open()) {
            controller.log_latency_matrix_csv(csv_file);
            csv_file.close();
            std::cout << "Latency matrix written to: " << config.output_csv_path << "\n";
        } else {
            std::cerr << "Error: Could not open output file: " << config.output_csv_path << "\n";
            // Fallback to stdout
            std::cout << "\n=== Latency Matrix (CSV) ===\n";
            controller.log_latency_matrix_csv(std::cout);
            std::cout << "=== End Latency Matrix ===\n";
        }

        // === Latency Sweep Tests (optional) ===
        if (config.run_latency_sweep && !g_shutdown_requested) {
            std::cout << "\n=== Executing Latency Sweep Tests ===\n";
            std::cout << "Message sizes: " << config.latency_message_sizes.size() << " sizes (8B to 64KB)\n";
            std::cout << "Iterations per size: " << config.latency_sweep_iterations << " (warmup: " << config.latency_sweep_warmup << ")\n";
            test_num = 0;

            // Test all unique pairs
            for (uint32_t i = 0; i < num_agents && !g_shutdown_requested; ++i) {
                for (uint32_t j = i + 1; j < num_agents && !g_shutdown_requested; ++j) {
                    if (!controller.buffer().is_agent_registered(i) ||
                        !controller.buffer().is_agent_registered(j)) {
                        continue;
                    }

                    ++test_num;
                    std::cout << "\n--- Latency Sweep " << test_num << ": " << i << " <-> " << j << " ---\n";

                    // Sweep through message sizes
                    for (uint64_t msg_size : config.latency_message_sizes) {
                        if (g_shutdown_requested) break;

                        // Validate message size fits in mailbox
                        if (msg_size > nixl_topo::MAILBOX_SIZE - sizeof(nixl_topo::MailboxHeader)) {
                            std::cerr << "  Skipping " << msg_size << " bytes: exceeds mailbox size\n";
                            continue;
                        }

                        if (controller.issue_ping_pong_test(i, j, msg_size,
                                                             config.latency_sweep_iterations,
                                                             config.latency_sweep_warmup)) {
                            if (controller.wait_for_results({i}, controller.command_seq(), result_timeout)) {
                                const auto* result = controller.get_result(i);
                                if (result) {
                                    uint64_t avg_lat = nixl_topo::from_wire64(result->avg_latency_ns);

                                    // Format message size for display
                                    std::string size_str;
                                    if (msg_size >= 1024) {
                                        size_str = std::to_string(msg_size / 1024) + "K";
                                    } else {
                                        size_str = std::to_string(msg_size) + "B";
                                    }

                                    std::cout << "  " << size_str << ": " << avg_lat << " ns\n";

                                    // Store detailed result
                                    controller.store_latency_detailed_result(i, j, msg_size, *result);
                                }
                            } else {
                                std::cerr << "  " << msg_size << "B: timeout\n";
                            }
                        } else {
                            std::cerr << "  " << msg_size << "B: failed to issue\n";
                        }
                    }
                }
            }

            std::cout << "\n=== Latency Sweep Complete ===\n";

            // Write detailed latency CSV
            std::ofstream lat_detailed_file(config.latency_detailed_csv_path);
            if (lat_detailed_file.is_open()) {
                controller.log_latency_detailed_csv(lat_detailed_file);
                lat_detailed_file.close();
                std::cout << "Latency sweep results written to: " << config.latency_detailed_csv_path << "\n";
            } else {
                std::cerr << "Error: Could not open output file: " << config.latency_detailed_csv_path << "\n";
            }
        }

        // === Bandwidth Tests ===
        if (config.run_bandwidth_tests && !g_shutdown_requested) {
            std::cout << "\n=== Executing Bandwidth Tests ===\n";
            std::cout << "Message sizes: " << config.bw_message_sizes.size() << " sizes\n";
            test_num = 0;

            // Bandwidth is unidirectional, so test both directions (i->j and j->i)
            // For each pair, sweep through all message sizes
            for (uint32_t i = 0; i < num_agents && !g_shutdown_requested; ++i) {
                for (uint32_t j = 0; j < num_agents && !g_shutdown_requested; ++j) {
                    if (i == j) continue;  // Skip self

                    if (!controller.buffer().is_agent_registered(i) ||
                        !controller.buffer().is_agent_registered(j)) {
                        continue;  // Skip unregistered agents
                    }

                    ++test_num;
                    std::cout << "\n--- Bandwidth Test " << test_num << ": sender=" << i
                              << " -> receiver=" << j << " (sweeping "
                              << config.bw_message_sizes.size() << " sizes) ---\n";

                    uint64_t peak_bw = 0;
                    uint64_t peak_msg_size = 0;

                    // Sweep through message sizes
                    for (uint64_t msg_size : config.bw_message_sizes) {
                        if (g_shutdown_requested) break;

                        // Check if window_size * msg_size fits in buffer
                        size_t required = static_cast<size_t>(config.bw_window_size) * msg_size;
                        uint32_t effective_window = config.bw_window_size;
                        if (required > nixl_topo::AGENT_COMMAND_INBOX_OFFSET) {
                            // Reduce window size to fit
                            effective_window = static_cast<uint32_t>(
                                nixl_topo::AGENT_COMMAND_INBOX_OFFSET / msg_size);
                            if (effective_window < 1) {
                                std::cerr << "  Skipping " << msg_size << " bytes: too large for buffer\n";
                                continue;
                            }
                            std::cout << "  Note: Using window=" << effective_window
                                      << " for " << msg_size << " byte messages\n";
                        }

                        if (controller.issue_bandwidth_test(i, j, msg_size,
                                                             config.bw_iterations, config.bw_warmup_iterations,
                                                             effective_window)) {
                            auto bw_timeout = std::chrono::seconds(config.result_timeout_sec * 2);
                            if (controller.wait_for_results({i}, controller.command_seq(), bw_timeout)) {
                                const auto* result = controller.get_result(i);
                                if (result) {
                                    uint64_t bw = nixl_topo::from_wire64(result->bandwidth_mbps);

                                    // Format message size for display
                                    std::string size_str;
                                    if (msg_size >= 1048576) {
                                        size_str = std::to_string(msg_size / 1048576) + "M";
                                    } else if (msg_size >= 1024) {
                                        size_str = std::to_string(msg_size / 1024) + "K";
                                    } else {
                                        size_str = std::to_string(msg_size);
                                    }

                                    std::cout << "  " << size_str << ": " << bw << " MB/s\n";

                                    // Track peak bandwidth
                                    if (bw > peak_bw) {
                                        peak_bw = bw;
                                        peak_msg_size = msg_size;
                                    }

                                    // Store result for this message size
                                    controller.store_bandwidth_result(i, j, msg_size, *result);
                                }
                            } else {
                                std::cerr << "  " << msg_size << ": timeout\n";
                            }
                        } else {
                            std::cerr << "  " << msg_size << ": failed to issue\n";
                        }
                    }

                    if (peak_bw > 0) {
                        std::cout << "  Peak: " << peak_bw << " MB/s @ " << peak_msg_size << " bytes\n";
                    }
                }
            }

            std::cout << "\n=== Bandwidth Tests Complete ===\n";

            // Write bandwidth matrix to CSV file (uses peak bandwidth per pair)
            std::ofstream bw_csv_file(config.bandwidth_csv_path);
            if (bw_csv_file.is_open()) {
                controller.log_bandwidth_matrix_csv(bw_csv_file);
                bw_csv_file.close();
                std::cout << "Bandwidth matrix written to: " << config.bandwidth_csv_path << "\n";
            } else {
                std::cerr << "Error: Could not open output file: " << config.bandwidth_csv_path << "\n";
            }

            // Write detailed bandwidth CSV (all message sizes)
            std::ofstream bw_detailed_file(config.bandwidth_detailed_csv_path);
            if (bw_detailed_file.is_open()) {
                controller.log_bandwidth_detailed_csv(bw_detailed_file);
                bw_detailed_file.close();
                std::cout << "Bandwidth details written to: " << config.bandwidth_detailed_csv_path << "\n";
            } else {
                std::cerr << "Error: Could not open output file: " << config.bandwidth_detailed_csv_path << "\n";
            }
        }
    }

    // Send SHUTDOWN command to all agents
    std::cout << "Sending shutdown to agents...\n";
    controller.shutdown_agents();

    // Give agents time to process shutdown and exit gracefully
    std::cout << "Waiting for agents to exit...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\nShutting down controller...\n";
    if (!g_harness_mode) {
        terminate_agents();
    }
    controller.shutdown();
    std::cout << "Controller shutdown complete.\n";

    return 0;
}
