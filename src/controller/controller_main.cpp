#include "controller.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <thread>
#include <unistd.h>

namespace {

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " -n <num_agents> [-t <timeout_sec>]\n"
              << "\n"
              << "Options:\n"
              << "  -n <num_agents>   Number of agents to wait for (required)\n"
              << "  -t <timeout_sec>  Timeout in seconds for agent registration (default: 60)\n"
              << "  -h                Show this help message\n";
}

volatile std::sig_atomic_t g_shutdown_requested = 0;

void signal_handler(int signal) {
    (void)signal;
    g_shutdown_requested = 1;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    uint32_t num_agents = 0;
    uint32_t timeout_sec = 60;

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

    // Print environment variables for each agent (full export commands)
    std::cout << "\nEnvironment variables for agent spawning:\n";
    std::cout << "==========================================\n";
    for (uint32_t i = 0; i < num_agents; ++i) {
        auto env_vars = controller.get_agent_env_vars(i);
        std::cout << "\n# Agent " << i << " - copy and run these exports, then run agent:\n";
        for (const auto& [key, value] : env_vars) {
            // Print full export command (use single quotes to prevent shell interpretation)
            std::cout << "export " << key << "='" << value << "'\n";
        }
    }
    std::cout << "\n==========================================\n\n";

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
        controller.shutdown();
        return 0;
    }

    // Signal rendezvous complete
    std::cout << "Signaling rendezvous complete to all agents...\n";
    controller.signal_rendezvous_complete();
    std::cout << "Rendezvous complete!\n";

    // Keep running until shutdown
    std::cout << "\nController running. Press Ctrl+C to shutdown.\n";
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down controller...\n";
    controller.shutdown();
    std::cout << "Controller shutdown complete.\n";

    return 0;
}
