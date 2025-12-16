#pragma once

#include "controller_buffer.hpp"
#include "../common/nixl_wrapper.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <chrono>

namespace nixl_topo {

/// Main controller class for agent bootstrap and coordination.
class Controller {
public:
    Controller();
    ~Controller();

    // Disable copy
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    /// Initialize the controller for the given number of agents.
    /// Sets up NIXL and allocates the shared buffer.
    /// @param num_agents Number of agents that will connect
    /// @return true on success
    bool initialize(uint32_t num_agents);

    /// Check if initialized.
    bool is_initialized() const { return initialized_; }

    /// Get number of agents.
    uint32_t num_agents() const { return num_agents_; }

    /// Get environment variables to pass when spawning an agent container.
    /// @param agent_id Agent ID (0 to num_agents-1)
    /// @return Map of environment variable name to value
    std::map<std::string, std::string> get_agent_env_vars(uint32_t agent_id) const;

    /// Wait for a specific agent to register (write its metadata).
    /// @param agent_id Agent ID to wait for
    /// @param timeout Maximum time to wait
    /// @return true if agent registered, false on timeout
    bool wait_for_agent(uint32_t agent_id, std::chrono::milliseconds timeout);

    /// Wait for all agents to register.
    /// @param timeout Maximum time to wait for all agents
    /// @return true if all agents registered, false on timeout
    bool wait_for_all_agents(std::chrono::milliseconds timeout);

    /// Signal all agents that rendezvous is complete.
    /// Writes RENDEZVOUS_COMPLETE notification to all agent notification slots.
    void signal_rendezvous_complete();

    /// Wait for all agents to complete peer discovery.
    /// @param timeout Maximum time to wait
    /// @return true if all agents reported peer discovery complete, false on timeout
    bool wait_for_peer_discovery(std::chrono::milliseconds timeout);

    /// Shutdown the controller and release resources.
    void shutdown();

    /// Get the controller buffer (for testing/debugging).
    const ControllerBuffer& buffer() const { return buffer_; }

    /// Issue a ping-pong latency test between two agents.
    /// Controller writes command to both agents' command inboxes via RDMA.
    /// @param initiator_id Agent that starts ping and measures RTT
    /// @param responder_id Agent that responds to pings
    /// @param message_size Payload size in bytes
    /// @param iterations Number of measured iterations
    /// @param warmup_iterations Number of warmup iterations (not measured)
    /// @return true if commands written successfully
    bool issue_ping_pong_test(uint32_t initiator_id, uint32_t responder_id,
                              uint64_t message_size, uint32_t iterations,
                              uint32_t warmup_iterations = 10);

    /// Send SHUTDOWN command to all registered agents.
    /// Agents will exit their command loops upon receiving this.
    /// @return true if commands sent successfully to all agents
    bool shutdown_agents();

    /// Wait for test results from specified agents.
    /// @param agent_ids Agents to wait for
    /// @param expected_seq Expected command_seq in results
    /// @param timeout Maximum time to wait
    /// @return true if all agents reported results, false on timeout
    bool wait_for_results(const std::vector<uint32_t>& agent_ids,
                          uint64_t expected_seq,
                          std::chrono::milliseconds timeout);

    /// Get current command sequence number.
    uint64_t command_seq() const { return command_seq_; }

    /// Get test result for an agent (in wire format, call from_wire() to convert).
    /// @param agent_id Agent to get result for
    /// @return Pointer to result, or nullptr if not available
    const TestResult* get_result(uint32_t agent_id) const;

private:
    /// Load metadata for a newly registered agent so we can send it notifications.
    bool load_agent_metadata(uint32_t agent_id);

    ControllerBuffer buffer_;
    NixlWrapper nixl_;
    uint32_t num_agents_ = 0;
    bool initialized_ = false;
    uint64_t notification_seq_ = 0;
    uint64_t command_seq_ = 0;
    std::vector<std::string> loaded_agent_names_;  // Agent names loaded for notifications
    std::vector<bool> agent_metadata_loaded_;       // Track which agents have been loaded
};

} // namespace nixl_topo
