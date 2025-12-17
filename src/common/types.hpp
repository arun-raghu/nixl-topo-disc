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
constexpr size_t AGENT_SLOT_HEADER_SIZE = 24;     // Fixed header in AgentSlot
constexpr size_t MAX_METADATA_BLOB_SIZE = AGENT_SLOT_SIZE - AGENT_SLOT_HEADER_SIZE;  // 4072
constexpr size_t NOTIFICATION_SLOT_SIZE = 64;     // Notification slot per agent

// =============================================================================
// Notification Types
// =============================================================================

enum class NotificationType : uint32_t {
    NONE = 0,
    RENDEZVOUS_COMPLETE = 1,
};

// =============================================================================
// Command Types (for test commands)
// =============================================================================

enum class CommandType : uint32_t {
    NONE = 0,
    SHUTDOWN = 1,
    PING_PONG_LATENCY = 2,
    BANDWIDTH = 3,              // Unidirectional streaming bandwidth test
};

// Agent role in test
enum class TestRole : uint32_t {
    NONE = 0,
    INITIATOR = 1,    // Starts the ping-pong, measures RTT
    RESPONDER = 2,    // Responds to pings
};

// Test status
enum class TestStatus : uint32_t {
    PENDING = 0,
    RUNNING = 1,
    COMPLETE = 2,
    ERROR = 3,
};

// =============================================================================
// Test Command/Result Constants
// =============================================================================

constexpr size_t COMMAND_INBOX_SIZE = 64;     // Where controller writes commands to agent
constexpr size_t MAILBOX_SIZE = 131072;       // For ping-pong data exchange (128KB, supports up to 64KB messages)
constexpr size_t TEST_RESULT_SIZE = 128;      // Result slot size per agent

// =============================================================================
// Buffer Header
// =============================================================================

struct BufferHeader {
    uint32_t magic;                 // Magic number for validation (0x4E49584C = "NIXL")
    uint32_t version;               // Buffer format version
    uint32_t num_agents;            // Total number of agents
    uint32_t agent_slots_offset;    // Offset to agent metadata region
    uint32_t notification_offset;   // Offset to notification region
    uint32_t result_offset;         // Offset to result region (VERSION 2+)
    uint64_t ready_flag;            // Set to 1 when rendezvous complete
    uint64_t buffer_base_addr;      // Absolute address of this buffer (for RDMA)

    static constexpr uint32_t MAGIC = 0x4E49584C;  // "NIXL"
    static constexpr uint32_t VERSION = 2;

    // Convert from wire format (big-endian) to host format
    void from_wire() {
        magic = from_wire32(magic);
        version = from_wire32(version);
        num_agents = from_wire32(num_agents);
        agent_slots_offset = from_wire32(agent_slots_offset);
        notification_offset = from_wire32(notification_offset);
        result_offset = from_wire32(result_offset);
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
        result_offset = to_wire32(result_offset);
        ready_flag = to_wire64(ready_flag);
        buffer_base_addr = to_wire64(buffer_base_addr);
    }
};

// =============================================================================
// Agent Metadata Slot
// =============================================================================

struct AgentSlot {
    uint64_t populated_flag;        // Non-zero when agent has written metadata
    uint64_t buffer_base_addr;      // Agent's test buffer address (for peer RDMA)
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
        buffer_base_addr = from_wire64(buffer_base_addr);
        metadata_size = from_wire32(metadata_size);
        reserved = from_wire32(reserved);
    }

    // Convert from host format to wire format (big-endian)
    void to_wire() {
        populated_flag = to_wire64(populated_flag);
        buffer_base_addr = to_wire64(buffer_base_addr);
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
// Test Command (written by controller to agent's command inbox)
// =============================================================================

struct TestCommand {
    uint64_t command_seq;       // 8 bytes - detect new commands
    CommandType command_type;   // 4 bytes
    TestRole role;              // 4 bytes - INITIATOR or RESPONDER
    uint32_t peer_agent_id;     // 4 bytes - the other agent in the test
    uint32_t warmup_iterations; // 4 bytes
    uint64_t message_size;      // 8 bytes - payload size per iteration
    uint32_t iterations;        // 4 bytes - measured iterations
    uint32_t window_size;       // 4 bytes - for BANDWIDTH: outstanding transfers
    uint8_t padding[24];        // 24 bytes - pad to 64
    // Note: peer_buffer_addr removed - agents get peer address from discover_peers()

    // Convert from wire format (big-endian) to host format
    void from_wire() {
        command_seq = from_wire64(command_seq);
        command_type = static_cast<CommandType>(from_wire32(static_cast<uint32_t>(command_type)));
        role = static_cast<TestRole>(from_wire32(static_cast<uint32_t>(role)));
        peer_agent_id = from_wire32(peer_agent_id);
        warmup_iterations = from_wire32(warmup_iterations);
        message_size = from_wire64(message_size);
        iterations = from_wire32(iterations);
        window_size = from_wire32(window_size);
    }

    // Convert from host format to wire format (big-endian)
    void to_wire() {
        command_seq = to_wire64(command_seq);
        command_type = static_cast<CommandType>(to_wire32(static_cast<uint32_t>(command_type)));
        role = static_cast<TestRole>(to_wire32(static_cast<uint32_t>(role)));
        peer_agent_id = to_wire32(peer_agent_id);
        warmup_iterations = to_wire32(warmup_iterations);
        message_size = to_wire64(message_size);
        iterations = to_wire32(iterations);
        window_size = to_wire32(window_size);
    }
};

static_assert(sizeof(TestCommand) == COMMAND_INBOX_SIZE,
              "TestCommand must equal COMMAND_INBOX_SIZE");

// =============================================================================
// Mailbox Header (for ping-pong synchronization)
// =============================================================================

struct MailboxHeader {
    uint64_t sequence;          // Incremented each write, polled by receiver
    uint64_t timestamp_ns;      // Optional: for timing
    // Followed by payload data up to (MAILBOX_SIZE - sizeof(MailboxHeader))
};

// =============================================================================
// Test Result (written by agent to controller's result region)
// =============================================================================

struct TestResult {
    uint64_t command_seq;          // 8 bytes - matches TestCommand.command_seq
    TestStatus status;             // 4 bytes
    uint32_t agent_id;             // 4 bytes - which agent wrote this
    uint64_t iterations_completed; // 8 bytes
    uint64_t min_latency_ns;       // 8 bytes
    uint64_t max_latency_ns;       // 8 bytes
    uint64_t avg_latency_ns;       // 8 bytes
    uint64_t total_bytes;          // 8 bytes - total data transferred
    uint64_t error_code;           // 8 bytes - 0 if no error
    uint64_t elapsed_ns;           // 8 bytes - total elapsed time (for bandwidth)
    uint64_t bandwidth_mbps;       // 8 bytes - bandwidth in MB/s (for bandwidth test)
    uint8_t padding[48];           // 48 bytes - pad to 128

    // Convert from wire format (big-endian) to host format
    void from_wire() {
        command_seq = from_wire64(command_seq);
        status = static_cast<TestStatus>(from_wire32(static_cast<uint32_t>(status)));
        agent_id = from_wire32(agent_id);
        iterations_completed = from_wire64(iterations_completed);
        min_latency_ns = from_wire64(min_latency_ns);
        max_latency_ns = from_wire64(max_latency_ns);
        avg_latency_ns = from_wire64(avg_latency_ns);
        total_bytes = from_wire64(total_bytes);
        error_code = from_wire64(error_code);
        elapsed_ns = from_wire64(elapsed_ns);
        bandwidth_mbps = from_wire64(bandwidth_mbps);
    }

    // Convert from host format to wire format (big-endian)
    void to_wire() {
        command_seq = to_wire64(command_seq);
        status = static_cast<TestStatus>(to_wire32(static_cast<uint32_t>(status)));
        agent_id = to_wire32(agent_id);
        iterations_completed = to_wire64(iterations_completed);
        min_latency_ns = to_wire64(min_latency_ns);
        max_latency_ns = to_wire64(max_latency_ns);
        avg_latency_ns = to_wire64(avg_latency_ns);
        total_bytes = to_wire64(total_bytes);
        error_code = to_wire64(error_code);
        elapsed_ns = to_wire64(elapsed_ns);
        bandwidth_mbps = to_wire64(bandwidth_mbps);
    }
};

static_assert(sizeof(TestResult) == TEST_RESULT_SIZE,
              "TestResult must equal TEST_RESULT_SIZE");

// =============================================================================
// Controller ID
// =============================================================================

constexpr uint32_t CONTROLLER_ID = UINT32_MAX;  // Special ID for controller

// =============================================================================
// Agent Buffer Constants
// =============================================================================

constexpr size_t DEFAULT_TEST_BUFFER_SIZE = 256 * 1024 * 1024;  // 256MB

// Offsets within agent's test buffer for command/mailbox regions
// These are at the END of the test buffer to not interfere with test data
constexpr size_t AGENT_COMMAND_INBOX_OFFSET = DEFAULT_TEST_BUFFER_SIZE - COMMAND_INBOX_SIZE - MAILBOX_SIZE;
constexpr size_t AGENT_MAILBOX_OFFSET = DEFAULT_TEST_BUFFER_SIZE - MAILBOX_SIZE;

// =============================================================================
// NIXL Agent Names
// =============================================================================

constexpr const char* CONTROLLER_NAME = "controller";

// =============================================================================
// Notification Message Types (for NIXL genNotif/getNotifs)
// =============================================================================

constexpr const char* NOTIF_AGENT_REGISTERED = "AGENT_REGISTERED";
constexpr const char* NOTIF_RENDEZVOUS_COMPLETE = "RENDEZVOUS_COMPLETE";
constexpr const char* NOTIF_PEER_DISCOVERY_COMPLETE = "PEER_DISCOVERY_COMPLETE";

} // namespace nixl_topo
