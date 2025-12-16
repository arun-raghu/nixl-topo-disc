#include "controller_buffer.hpp"
#include "../common/memory.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace nixl_topo {

ControllerBuffer::ControllerBuffer() = default;

ControllerBuffer::~ControllerBuffer() {
    deallocate();
}

ControllerBuffer::ControllerBuffer(ControllerBuffer&& other) noexcept
    : buffer_(other.buffer_),
      buffer_size_(other.buffer_size_),
      num_agents_(other.num_agents_),
      agent_slots_offset_(other.agent_slots_offset_),
      notification_offset_(other.notification_offset_),
      result_offset_(other.result_offset_),
      cmd_area_offset_(other.cmd_area_offset_) {
    other.buffer_ = nullptr;
    other.buffer_size_ = 0;
    other.num_agents_ = 0;
    other.agent_slots_offset_ = 0;
    other.notification_offset_ = 0;
    other.result_offset_ = 0;
    other.cmd_area_offset_ = 0;
}

ControllerBuffer& ControllerBuffer::operator=(ControllerBuffer&& other) noexcept {
    if (this != &other) {
        deallocate();
        buffer_ = other.buffer_;
        buffer_size_ = other.buffer_size_;
        num_agents_ = other.num_agents_;
        agent_slots_offset_ = other.agent_slots_offset_;
        notification_offset_ = other.notification_offset_;
        result_offset_ = other.result_offset_;
        cmd_area_offset_ = other.cmd_area_offset_;
        other.buffer_ = nullptr;
        other.buffer_size_ = 0;
        other.num_agents_ = 0;
        other.agent_slots_offset_ = 0;
        other.notification_offset_ = 0;
        other.result_offset_ = 0;
        other.cmd_area_offset_ = 0;
    }
    return *this;
}

bool ControllerBuffer::allocate(uint32_t num_agents) {
    if (buffer_ != nullptr) {
        return false;  // Already allocated
    }

    if (num_agents == 0) {
        return false;
    }

    // Calculate buffer layout sizes
    const size_t header_size = sizeof(BufferHeader);
    const size_t agent_slots_size = num_agents * AGENT_SLOT_SIZE;
    const size_t notification_size = num_agents * NOTIFICATION_SLOT_SIZE;
    const size_t result_size = num_agents * TEST_RESULT_SIZE;
    const size_t cmd_area_size = CONTROLLER_CMD_AREA_SIZE;
    const size_t total_size = header_size + agent_slots_size + notification_size + result_size + cmd_area_size;

    // Allocate pinned, page-aligned memory (always tries to pin; mlock failure is non-fatal)
    void* buffer = alloc_buffer(total_size, true, false);
    if (!buffer) {
        return false;
    }

    // Zero-initialize
    std::memset(buffer, 0, total_size);

    // Cache offsets in host format for local access
    agent_slots_offset_ = static_cast<uint32_t>(header_size);
    notification_offset_ = static_cast<uint32_t>(header_size + agent_slots_size);
    result_offset_ = static_cast<uint32_t>(header_size + agent_slots_size + notification_size);
    cmd_area_offset_ = static_cast<uint32_t>(header_size + agent_slots_size + notification_size + result_size);

    // Initialize header in wire format (big-endian for cross-platform RDMA)
    auto* hdr = reinterpret_cast<BufferHeader*>(buffer);
    hdr->magic = BufferHeader::MAGIC;
    hdr->version = BufferHeader::VERSION;
    hdr->num_agents = num_agents;
    hdr->agent_slots_offset = agent_slots_offset_;
    hdr->notification_offset = notification_offset_;
    hdr->result_offset = result_offset_;
    hdr->ready_flag = 0;
    hdr->buffer_base_addr = reinterpret_cast<uint64_t>(buffer);  // For RDMA absolute addressing
    hdr->to_wire();  // Convert to network byte order

    buffer_ = buffer;
    buffer_size_ = total_size;
    num_agents_ = num_agents;

    return true;
}

void ControllerBuffer::deallocate() {
    if (buffer_ != nullptr) {
        free_buffer(buffer_, buffer_size_);
        buffer_ = nullptr;
        buffer_size_ = 0;
        num_agents_ = 0;
        agent_slots_offset_ = 0;
        notification_offset_ = 0;
        result_offset_ = 0;
        cmd_area_offset_ = 0;
    }
}

BufferHeader* ControllerBuffer::header() {
    if (!buffer_) return nullptr;
    return reinterpret_cast<BufferHeader*>(buffer_);
}

const BufferHeader* ControllerBuffer::header() const {
    if (!buffer_) return nullptr;
    return reinterpret_cast<const BufferHeader*>(buffer_);
}

AgentSlot* ControllerBuffer::agent_slot(uint32_t agent_id) {
    if (!buffer_ || agent_id >= num_agents_) return nullptr;

    auto* base = reinterpret_cast<uint8_t*>(buffer_);
    return reinterpret_cast<AgentSlot*>(base + agent_slots_offset_ +
                                        agent_id * AGENT_SLOT_SIZE);
}

const AgentSlot* ControllerBuffer::agent_slot(uint32_t agent_id) const {
    if (!buffer_ || agent_id >= num_agents_) return nullptr;

    auto* base = reinterpret_cast<const uint8_t*>(buffer_);
    return reinterpret_cast<const AgentSlot*>(base + agent_slots_offset_ +
                                              agent_id * AGENT_SLOT_SIZE);
}

Notification* ControllerBuffer::notification_slot(uint32_t agent_id) {
    if (!buffer_ || agent_id >= num_agents_) return nullptr;

    auto* base = reinterpret_cast<uint8_t*>(buffer_);
    return reinterpret_cast<Notification*>(base + notification_offset_ +
                                           agent_id * NOTIFICATION_SLOT_SIZE);
}

const Notification* ControllerBuffer::notification_slot(uint32_t agent_id) const {
    if (!buffer_ || agent_id >= num_agents_) return nullptr;

    auto* base = reinterpret_cast<const uint8_t*>(buffer_);
    return reinterpret_cast<const Notification*>(base + notification_offset_ +
                                                 agent_id * NOTIFICATION_SLOT_SIZE);
}

bool ControllerBuffer::is_agent_registered(uint32_t agent_id) const {
    const auto* wire_slot = agent_slot(agent_id);
    if (!wire_slot) return false;
    // Convert populated_flag from wire format
    return from_wire64(wire_slot->populated_flag) != 0;
}

void ControllerBuffer::set_ready_flag() {
    auto* hdr = header();
    if (hdr) {
        // Write ready_flag in wire format
        hdr->ready_flag = to_wire64(1);
    }
}

TestResult* ControllerBuffer::result_slot(uint32_t agent_id) {
    if (!buffer_ || agent_id >= num_agents_) return nullptr;

    auto* base = reinterpret_cast<uint8_t*>(buffer_);
    return reinterpret_cast<TestResult*>(base + result_offset_ +
                                          agent_id * TEST_RESULT_SIZE);
}

const TestResult* ControllerBuffer::result_slot(uint32_t agent_id) const {
    if (!buffer_ || agent_id >= num_agents_) return nullptr;

    auto* base = reinterpret_cast<const uint8_t*>(buffer_);
    return reinterpret_cast<const TestResult*>(base + result_offset_ +
                                                agent_id * TEST_RESULT_SIZE);
}

void* ControllerBuffer::cmd_slot(uint32_t slot_index) {
    if (!buffer_) return nullptr;

    // Each slot is 64 bytes (size of TestCommand)
    constexpr size_t CMD_SLOT_SIZE = 64;
    size_t offset = slot_index * CMD_SLOT_SIZE;

    if (offset + CMD_SLOT_SIZE > CONTROLLER_CMD_AREA_SIZE) {
        return nullptr;  // Out of bounds
    }

    auto* base = reinterpret_cast<uint8_t*>(buffer_);
    return base + cmd_area_offset_ + offset;
}

} // namespace nixl_topo
