#include "agent.hpp"
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void signal_handler(int signal) {
    (void)signal;
    g_shutdown_requested = 1;
}

} // anonymous namespace

int main() {
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "Agent starting...\n";

    // Create and initialize agent
    nixl_topo::Agent agent;

    if (!agent.initialize()) {
        std::cerr << "Error: Failed to initialize agent\n";
        std::cerr << "  Ensure CTRL_METADATA, AGENT_ID, and NUM_AGENTS environment variables are set\n";
        return 1;
    }

    std::cout << "Agent " << agent.agent_id() << " initialized successfully\n";
    std::cout << "  Total agents: " << agent.num_agents() << "\n";
    std::cout << "  Test buffer size: " << (agent.test_buffer_size() / (1024 * 1024)) << " MB\n";

    // Register with controller
    std::cout << "Registering with controller...\n";
    if (!agent.register_with_controller()) {
        std::cerr << "Error: Failed to register with controller\n";
        agent.shutdown();
        return 1;
    }

    std::cout << "Registration complete, waiting for rendezvous...\n";

    // Wait for rendezvous completion
    constexpr auto rendezvous_timeout = std::chrono::seconds(60);
    if (!agent.wait_for_rendezvous(rendezvous_timeout)) {
        std::cerr << "Error: Timeout waiting for rendezvous completion\n";
        agent.shutdown();
        return 1;
    }

    std::cout << "Rendezvous complete!\n";

    // Keep running until shutdown signal
    std::cout << "Agent running. Press Ctrl+C to shutdown.\n";
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down agent...\n";
    agent.shutdown();
    std::cout << "Agent shutdown complete.\n";

    return 0;
}
