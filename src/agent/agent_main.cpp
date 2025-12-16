#include "agent.hpp"
#include <iostream>
#include <cstdio>
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
    // Disable stdout buffering for immediate log output
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

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
    std::cout << "  Test buffer address: 0x" << std::hex << reinterpret_cast<uintptr_t>(agent.test_buffer())
              << std::dec << "\n";

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

    // Discover peers
    std::cout << "Discovering peers...\n";
    if (!agent.discover_peers()) {
        std::cerr << "Error: Failed to discover peers\n";
        agent.shutdown();
        return 1;
    }
    std::cout << "Peer discovery complete.\n";

#ifdef ENABLE_PEER_TRANSFER_TEST
    // Verify data transfer with one peer (agent 0 tests with agent 1)
    if (agent.num_agents() > 1 && agent.agent_id() == 0) {
        uint32_t peer_id = 1;
        std::cout << "Verifying data transfer with agent_" << peer_id << "...\n";
        if (agent.verify_peer_transfer(peer_id, 4096)) {
            std::cout << "Data transfer verification SUCCESS with agent_" << peer_id << "\n";
        } else {
            std::cerr << "Data transfer verification FAILED with agent_" << peer_id << "\n";
        }
    }
#endif

    // Run command loop, waiting for test commands from controller
    std::cout << "Agent running, waiting for commands...\n";
    agent.run_command_loop(g_shutdown_requested);

    std::cout << "\nShutting down agent...\n";
    agent.shutdown();
    std::cout << "Agent shutdown complete.\n";

    return 0;
}
