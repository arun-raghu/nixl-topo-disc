#pragma once

#include "../common/nixl_wrapper.hpp"
#include <cstdint>
#include <chrono>
#include <string>

namespace nixl_topo {

/// Agent class for NIXL registration and controller rendezvous.
/// Handles bootstrap from environment variables, buffer allocation,
/// and coordination with the controller.
class Agent {
public:
    Agent();
    ~Agent();

    // Disable copy
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    /// Initialize the agent from environment variables.
    /// Reads CTRL_METADATA, AGENT_ID, NUM_AGENTS from environment.
    /// Allocates and registers test buffer, connects to controller.
    /// @return true on success
    bool initialize();

    /// Register with the controller by writing metadata to AgentSlot.
    /// @return true on success
    bool register_with_controller();

    /// Wait for RENDEZVOUS_COMPLETE notification from controller.
    /// @param timeout Maximum time to wait
    /// @return true if rendezvous completed, false on timeout
    bool wait_for_rendezvous(std::chrono::milliseconds timeout);

    /// Shutdown the agent and release resources.
    void shutdown();

    // Accessors
    uint32_t agent_id() const { return agent_id_; }
    uint32_t num_agents() const { return num_agents_; }
    bool is_initialized() const { return initialized_; }
    bool is_rendezvous_complete() const { return rendezvous_complete_; }

    /// Get the test buffer (for future use).
    void* test_buffer() { return test_buffer_; }
    size_t test_buffer_size() const { return test_buffer_size_; }

private:
    NixlWrapper nixl_;
    void* test_buffer_ = nullptr;
    size_t test_buffer_size_ = 0;

    uint32_t agent_id_ = 0;
    uint32_t num_agents_ = 0;
    std::string controller_name_;

    // Controller buffer info (read from BufferHeader)
    uint64_t ctrl_buffer_base_addr_ = 0;  // Controller's buffer base address for RDMA
    uint32_t ctrl_agent_slots_offset_ = 0;
    uint32_t ctrl_notification_offset_ = 0;

    bool initialized_ = false;
    bool rendezvous_complete_ = false;
};

} // namespace nixl_topo
