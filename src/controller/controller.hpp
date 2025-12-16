#pragma once

#include "controller_buffer.hpp"
#include "../common/nixl_wrapper.hpp"
#include <cstdint>
#include <map>
#include <string>
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

    /// Shutdown the controller and release resources.
    void shutdown();

    /// Get the controller buffer (for testing/debugging).
    const ControllerBuffer& buffer() const { return buffer_; }

private:
    /// Load metadata for a newly registered agent so we can send it notifications.
    bool load_agent_metadata(uint32_t agent_id);

    ControllerBuffer buffer_;
    NixlWrapper nixl_;
    uint32_t num_agents_ = 0;
    bool initialized_ = false;
    uint64_t notification_seq_ = 0;
    std::vector<std::string> loaded_agent_names_;  // Agent names loaded for notifications
    std::vector<bool> agent_metadata_loaded_;       // Track which agents have been loaded
};

} // namespace nixl_topo
