#pragma once

#include "../common/nixl_wrapper.hpp"
#include "../common/types.hpp"
#include <cstdint>
#include <chrono>
#include <csignal>
#include <string>
#include <vector>

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

    /// Discover all peer agents after rendezvous.
    /// Reads peer AgentSlots from controller, loads their NIXL metadata.
    /// @return true if all peers discovered successfully
    bool discover_peers();

    /// Write data to a peer agent's buffer.
    /// @param peer_id Target agent ID
    /// @param offset Offset within peer's buffer
    /// @param data Source data
    /// @param size Bytes to write
    /// @return true on success
    bool write_to_peer(uint32_t peer_id, size_t offset, const void* data, size_t size);

    /// Read data from a peer agent's buffer.
    /// @param peer_id Source agent ID
    /// @param offset Offset within peer's buffer
    /// @param data Destination buffer
    /// @param size Bytes to read
    /// @return true on success
    bool read_from_peer(uint32_t peer_id, size_t offset, void* data, size_t size);

    /// Verify data transfer with a peer: write pattern, read back, compare.
    /// @param peer_id Target agent ID
    /// @param size Bytes to transfer
    /// @return true if data matches
    bool verify_peer_transfer(uint32_t peer_id, size_t size);

    // Accessors
    uint32_t agent_id() const { return agent_id_; }
    uint32_t num_agents() const { return num_agents_; }
    bool is_initialized() const { return initialized_; }
    bool is_rendezvous_complete() const { return rendezvous_complete_; }

    /// Get the test buffer (for future use).
    void* test_buffer() { return test_buffer_; }
    size_t test_buffer_size() const { return test_buffer_size_; }

    /// Poll command inbox and execute if new command found.
    /// @return true if a command was executed, false if no new command
    bool poll_and_execute_command();

    /// Run command polling loop until shutdown requested.
    /// @param shutdown_flag Reference to shutdown flag (set by signal handler)
    void run_command_loop(volatile std::sig_atomic_t& shutdown_flag);

private:
    /// Execute a ping-pong latency test.
    /// @param cmd The test command (already converted from wire format)
    /// @return TestResult with latency statistics
    TestResult execute_ping_pong(const TestCommand& cmd);

    /// Write result to controller's result slot (initiator only).
    /// @param result The result to write
    /// @return true on success
    bool report_result(const TestResult& result);

private:
    // Peer info discovered after rendezvous
    struct PeerInfo {
        std::string nixl_name;          // "agent_X"
        uintptr_t buffer_addr = 0;      // Peer's buffer address
        bool connected = false;         // NIXL metadata loaded
    };

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
    uint32_t ctrl_result_offset_ = 0;     // Result region offset in controller buffer

    // Command tracking
    uint64_t last_command_seq_ = 0;       // Last processed command sequence

    // Peer info discovered after rendezvous
    std::vector<PeerInfo> peers_;

    bool initialized_ = false;
    bool rendezvous_complete_ = false;
};

} // namespace nixl_topo
