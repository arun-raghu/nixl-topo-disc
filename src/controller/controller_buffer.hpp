#pragma once

#include "../common/types.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace nixl_topo {

/// Size of command scratch area (enough for multiple parallel commands)
constexpr size_t CONTROLLER_CMD_AREA_SIZE = 4096;  // 64 commands @ 64 bytes each

/// Manages the controller's shared buffer for agent registration and coordination.
/// Layout:
///   [BufferHeader][AgentSlots][Notifications][Results][CmdArea]
class ControllerBuffer {
public:
    ControllerBuffer();
    ~ControllerBuffer();

    // Disable copy
    ControllerBuffer(const ControllerBuffer&) = delete;
    ControllerBuffer& operator=(const ControllerBuffer&) = delete;

    // Enable move
    ControllerBuffer(ControllerBuffer&&) noexcept;
    ControllerBuffer& operator=(ControllerBuffer&&) noexcept;

    /// Allocate and initialize buffer for the given number of agents.
    /// Returns true on success.
    bool allocate(uint32_t num_agents);

    /// Deallocate buffer.
    void deallocate();

    /// Check if buffer is allocated.
    bool is_allocated() const { return buffer_ != nullptr; }

    /// Get number of agents this buffer was allocated for.
    uint32_t num_agents() const { return num_agents_; }

    /// Get total buffer size in bytes.
    size_t size() const { return buffer_size_; }

    /// Get raw pointer to buffer (for NIXL registration).
    void* data() { return buffer_; }
    const void* data() const { return buffer_; }

    /// Get pointer to buffer header.
    BufferHeader* header();
    const BufferHeader* header() const;

    /// Get pointer to agent metadata slot.
    AgentSlot* agent_slot(uint32_t agent_id);
    const AgentSlot* agent_slot(uint32_t agent_id) const;

    /// Get pointer to agent notification slot.
    Notification* notification_slot(uint32_t agent_id);
    const Notification* notification_slot(uint32_t agent_id) const;

    /// Check if agent has populated its metadata slot.
    bool is_agent_registered(uint32_t agent_id) const;

    /// Set the ready flag to signal rendezvous complete.
    void set_ready_flag();

    /// Get pointer to agent result slot.
    TestResult* result_slot(uint32_t agent_id);
    const TestResult* result_slot(uint32_t agent_id) const;

    /// Get pointer to command scratch area (for RDMA writes to agents).
    /// @param slot_index Index into the command area (0-63 for 64-byte commands)
    void* cmd_slot(uint32_t slot_index = 0);

private:
    void* buffer_ = nullptr;
    size_t buffer_size_ = 0;
    uint32_t num_agents_ = 0;

    // Cached offsets in host format (header is stored in wire format for RDMA)
    uint32_t agent_slots_offset_ = 0;
    uint32_t notification_offset_ = 0;
    uint32_t result_offset_ = 0;
    uint32_t cmd_area_offset_ = 0;
};

} // namespace nixl_topo
