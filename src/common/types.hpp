#pragma once

#include <cstdint>
#include <cstddef>
#include <endian.h>

namespace nixl_topo {

// =============================================================================
// Endian Conversion Helpers (network byte order = big-endian)
// =============================================================================

inline uint32_t to_wire32(uint32_t host) { return htobe32(host); }
inline uint32_t from_wire32(uint32_t wire) { return be32toh(wire); }
inline uint64_t to_wire64(uint64_t host) { return htobe64(host); }
inline uint64_t from_wire64(uint64_t wire) { return be64toh(wire); }

// =============================================================================
// Buffer Layout Constants
// =============================================================================

constexpr size_t AGENT_SLOT_SIZE = 4096;          // Metadata slot per agent
constexpr size_t AGENT_SLOT_HEADER_SIZE = 16;     // Fixed header in AgentSlot
constexpr size_t MAX_METADATA_BLOB_SIZE = AGENT_SLOT_SIZE - AGENT_SLOT_HEADER_SIZE;  // 4080
constexpr size_t NOTIFICATION_SLOT_SIZE = 64;     // Notification slot per agent

// =============================================================================
// Notification Types
// =============================================================================

enum class NotificationType : uint32_t {
    NONE = 0,
    RENDEZVOUS_COMPLETE = 1,
};

// =============================================================================
// Buffer Header
// =============================================================================

struct BufferHeader {
    uint32_t magic;                 // Magic number for validation (0x4E49584C = "NIXL")
    uint32_t version;               // Buffer format version
    uint32_t num_agents;            // Total number of agents
    uint32_t agent_slots_offset;    // Offset to agent metadata region
    uint32_t notification_offset;   // Offset to notification region
    uint64_t ready_flag;            // Set to 1 when rendezvous complete
    uint64_t buffer_base_addr;      // Absolute address of this buffer (for RDMA)

    static constexpr uint32_t MAGIC = 0x4E49584C;  // "NIXL"
    static constexpr uint32_t VERSION = 1;

    // Convert from wire format (big-endian) to host format
    void from_wire() {
        magic = from_wire32(magic);
        version = from_wire32(version);
        num_agents = from_wire32(num_agents);
        agent_slots_offset = from_wire32(agent_slots_offset);
        notification_offset = from_wire32(notification_offset);
        ready_flag = from_wire64(ready_flag);
        buffer_base_addr = from_wire64(buffer_base_addr);
    }

    // Convert from host format to wire format (big-endian)
    void to_wire() {
        magic = to_wire32(magic);
        version = to_wire32(version);
        num_agents = to_wire32(num_agents);
        agent_slots_offset = to_wire32(agent_slots_offset);
        notification_offset = to_wire32(notification_offset);
        ready_flag = to_wire64(ready_flag);
        buffer_base_addr = to_wire64(buffer_base_addr);
    }
};

// =============================================================================
// Agent Metadata Slot
// =============================================================================

struct AgentSlot {
    uint64_t populated_flag;        // Non-zero when agent has written metadata
    uint32_t metadata_size;         // Size of the metadata blob
    uint32_t reserved;              // Padding for alignment
    // Followed by: uint8_t metadata_blob[MAX_METADATA_BLOB_SIZE]
    // Contains serialized NIXL endpoint + buffer descriptor

    // Get pointer to metadata blob (immediately follows this struct)
    uint8_t* metadata() {
        return reinterpret_cast<uint8_t*>(this + 1);
    }

    const uint8_t* metadata() const {
        return reinterpret_cast<const uint8_t*>(this + 1);
    }

    // Convert from wire format (big-endian) to host format
    void from_wire() {
        populated_flag = from_wire64(populated_flag);
        metadata_size = from_wire32(metadata_size);
        reserved = from_wire32(reserved);
    }

    // Convert from host format to wire format (big-endian)
    void to_wire() {
        populated_flag = to_wire64(populated_flag);
        metadata_size = to_wire32(metadata_size);
        reserved = to_wire32(reserved);
    }
};

static_assert(sizeof(AgentSlot) == AGENT_SLOT_HEADER_SIZE,
              "AgentSlot size must match AGENT_SLOT_HEADER_SIZE");

// =============================================================================
// Notification Slot
// =============================================================================

struct Notification {
    uint64_t sequence;              // Monotonically increasing sequence number
    NotificationType type;          // Type of notification
    uint32_t source_id;             // Source agent/controller ID
    uint64_t timestamp_ns;          // Timestamp in nanoseconds (optional)
    uint64_t payload;               // Type-specific payload
    uint8_t padding[32];            // Pad to NOTIFICATION_SLOT_SIZE

    Notification() : sequence(0), type(NotificationType::NONE),
                     source_id(0), timestamp_ns(0), payload(0), padding{} {}
};

static_assert(sizeof(Notification) == NOTIFICATION_SLOT_SIZE,
              "Notification must equal NOTIFICATION_SLOT_SIZE");

// =============================================================================
// Controller ID
// =============================================================================

constexpr uint32_t CONTROLLER_ID = UINT32_MAX;  // Special ID for controller

// =============================================================================
// Agent Buffer Constants
// =============================================================================

constexpr size_t DEFAULT_TEST_BUFFER_SIZE = 256 * 1024 * 1024;  // 256MB

// =============================================================================
// NIXL Agent Names
// =============================================================================

constexpr const char* CONTROLLER_NAME = "controller";

// =============================================================================
// Notification Message Types (for NIXL genNotif/getNotifs)
// =============================================================================

constexpr const char* NOTIF_AGENT_REGISTERED = "AGENT_REGISTERED";
constexpr const char* NOTIF_RENDEZVOUS_COMPLETE = "RENDEZVOUS_COMPLETE";

} // namespace nixl_topo
