#include "controller.hpp"
#include <iostream>
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

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " -n <num_agents> [-t <timeout_sec>] [-a <agent_path>]\n"
              << "\n"
              << "Options:\n"
              << "  -n <num_agents>   Number of agents to wait for (required)\n"
              << "  -t <timeout_sec>  Timeout in seconds for agent registration (default: 60)\n"
              << "  -a <agent_path>   Path to agent binary (default: ./agent in same dir as controller)\n"
              << "  -h                Show this help message\n";
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

volatile std::sig_atomic_t g_shutdown_requested = 0;

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

    // Determine agent binary path
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

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "Starting controller for " << num_agents << " agents\n";
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

    // Spawn agent processes
    std::cout << "\nSpawning " << num_agents << " agents...\n";
    std::cout << "Agent binary: " << g_agent_binary_path << "\n";
    for (uint32_t i = 0; i < num_agents; ++i) {
        auto env_vars = controller.get_agent_env_vars(i);
        if (!spawn_agent(i, env_vars)) {
            std::cerr << "Error: Failed to spawn agent " << i << "\n";
            terminate_agents();
            controller.shutdown();
            return 1;
        }
    }
    std::cout << "All agents spawned. Logs in /tmp/agent_*.log\n\n";

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

            terminate_agents();
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
        terminate_agents();
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
        terminate_agents();
        controller.shutdown();
        return 1;
    }

    // Execute tests
    // TODO: Later, read test configuration from cmd_config file provided at startup
    // For now, issue a single ping-pong test between lowest numbered registered agents
    if (!g_shutdown_requested && num_agents >= 2) {
        std::cout << "\n=== Executing Tests ===\n";

        // Find two lowest-numbered registered agents
        uint32_t initiator_id = UINT32_MAX;
        uint32_t responder_id = UINT32_MAX;
        for (uint32_t i = 0; i < num_agents && responder_id == UINT32_MAX; ++i) {
            if (controller.buffer().is_agent_registered(i)) {
                if (initiator_id == UINT32_MAX) {
                    initiator_id = i;
                } else {
                    responder_id = i;
                }
            }
        }

        if (initiator_id != UINT32_MAX && responder_id != UINT32_MAX) {
            // Issue ping-pong test with default parameters
            uint64_t message_size = 64;
            uint32_t iterations = 100;
            uint32_t warmup_iterations = 10;
            auto result_timeout = std::chrono::seconds(30);

            // Test 1: initiator_id -> responder_id
            std::cout << "\n--- Test 1: initiator=" << initiator_id
                      << " responder=" << responder_id << " ---\n";

            if (controller.issue_ping_pong_test(initiator_id, responder_id,
                                                 message_size, iterations, warmup_iterations)) {
                std::cout << "Waiting for results...\n";
                if (controller.wait_for_results({initiator_id}, controller.command_seq(), result_timeout)) {
                    std::cout << "Test 1 completed successfully!\n";
                } else {
                    std::cerr << "Error: Timeout waiting for test 1 results\n";
                }
            } else {
                std::cerr << "Error: Failed to issue test 1\n";
            }

            // Test 2: swap roles - responder_id -> initiator_id
            std::cout << "\n--- Test 2: initiator=" << responder_id
                      << " responder=" << initiator_id << " ---\n";

            if (controller.issue_ping_pong_test(responder_id, initiator_id,
                                                 message_size, iterations, warmup_iterations)) {
                std::cout << "Waiting for results...\n";
                if (controller.wait_for_results({responder_id}, controller.command_seq(), result_timeout)) {
                    std::cout << "Test 2 completed successfully!\n";
                } else {
                    std::cerr << "Error: Timeout waiting for test 2 results\n";
                }
            } else {
                std::cerr << "Error: Failed to issue test 2\n";
            }
        } else {
            std::cerr << "Error: Not enough registered agents for ping-pong test\n";
        }

        std::cout << "\n=== Tests Complete ===\n\n";
    }

    // Send SHUTDOWN command to all agents
    std::cout << "Sending shutdown to agents...\n";
    controller.shutdown_agents();

    // Give agents time to process shutdown and exit gracefully
    std::cout << "Waiting for agents to exit...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\nShutting down controller...\n";
    terminate_agents();
    controller.shutdown();
    std::cout << "Controller shutdown complete.\n";

    return 0;
}
