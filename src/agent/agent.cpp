#include "agent.hpp"
#include "../common/types.hpp"
#include "../common/memory.hpp"
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

namespace nixl_topo {

Agent::Agent() = default;

Agent::~Agent() {
    shutdown();
}

bool Agent::initialize() {
    if (initialized_) {
        std::cerr << "Agent::initialize: already initialized\n";
        return false;
    }

    // Step 1: Read environment variables
    const char* ctrl_metadata_env = std::getenv("CTRL_METADATA");
    const char* ctrl_buffer_addr_env = std::getenv("CTRL_BUFFER_ADDR");
    const char* agent_id_env = std::getenv("AGENT_ID");
    const char* num_agents_env = std::getenv("NUM_AGENTS");

    if (!ctrl_metadata_env || !ctrl_buffer_addr_env || !agent_id_env || !num_agents_env) {
        std::cerr << "Agent::initialize: missing environment variables:";
        if (!ctrl_metadata_env) std::cerr << " CTRL_METADATA";
        if (!ctrl_buffer_addr_env) std::cerr << " CTRL_BUFFER_ADDR";
        if (!agent_id_env) std::cerr << " AGENT_ID";
        if (!num_agents_env) std::cerr << " NUM_AGENTS";
        std::cerr << "\n";
        return false;
    }

    // Parse controller buffer base address (big-endian for cross-platform compatibility)
    ctrl_buffer_base_addr_ = from_wire64(std::strtoull(ctrl_buffer_addr_env, nullptr, 0));

    agent_id_ = static_cast<uint32_t>(std::strtoul(agent_id_env, nullptr, 10));
    num_agents_ = static_cast<uint32_t>(std::strtoul(num_agents_env, nullptr, 10));

    if (num_agents_ == 0) {
        std::cerr << "Agent::initialize: NUM_AGENTS must be > 0 (got '" << num_agents_env << "')\n";
        return false;
    }

    // Step 2: Initialize NIXL
    std::string agent_name = "agent_" + std::to_string(agent_id_);
    if (!nixl_.initialize(agent_name)) {
        std::cerr << "Agent::initialize: nixl_.initialize('" << agent_name << "') failed\n";
        return false;
    }

    // Step 3: Allocate pinned, page-aligned test buffer
    // Always try to pin; mlock failure just means suboptimal RDMA performance
    test_buffer_size_ = DEFAULT_TEST_BUFFER_SIZE;
    test_buffer_ = alloc_buffer(test_buffer_size_, true, false);  // pin=true, warn_on_no_pin=false
    if (!test_buffer_) {
        std::cerr << "Agent::initialize: alloc_buffer(" << test_buffer_size_
                  << ") failed\n";
        nixl_.shutdown();
        return false;
    }
    std::memset(test_buffer_, 0, test_buffer_size_);

    // Step 4: Register buffer with NIXL
    if (!nixl_.register_buffer(test_buffer_, test_buffer_size_)) {
        std::cerr << "Agent::initialize: nixl_.register_buffer() failed\n";
        free_buffer(test_buffer_, test_buffer_size_);
        test_buffer_ = nullptr;
        nixl_.shutdown();
        return false;
    }

    // Step 5: Load controller's metadata
    if (!nixl_.load_remote_metadata(ctrl_metadata_env, controller_name_)) {
        std::cerr << "Agent::initialize: nixl_.load_remote_metadata() failed\n";
        nixl_.deregister_buffer();
        free_buffer(test_buffer_, test_buffer_size_);
        test_buffer_ = nullptr;
        nixl_.shutdown();
        return false;
    }

    // Step 6: Read BufferHeader from controller (using absolute address)
    // Note: RDMA requires local buffer to be registered, so use our test_buffer as scratch
    static_assert(sizeof(BufferHeader) <= DEFAULT_TEST_BUFFER_SIZE,
                  "BufferHeader must fit in test buffer");

    if (!nixl_.read_from_remote(controller_name_, test_buffer_, ctrl_buffer_base_addr_, sizeof(BufferHeader))) {
        std::cerr << "Agent::initialize: nixl_.read_from_remote('" << controller_name_
                  << "', addr=0x" << std::hex << ctrl_buffer_base_addr_
                  << ", size=" << std::dec << sizeof(BufferHeader) << ") failed\n";
        nixl_.deregister_buffer();
        free_buffer(test_buffer_, test_buffer_size_);
        test_buffer_ = nullptr;
        nixl_.shutdown();
        return false;
    }

    // Copy header from registered buffer and convert from wire format
    BufferHeader header;
    std::memcpy(&header, test_buffer_, sizeof(header));
    header.from_wire();

    // Step 7: Validate magic number
    if (header.magic != BufferHeader::MAGIC) {
        std::cerr << "Agent::initialize: invalid BufferHeader magic (got 0x"
                  << std::hex << header.magic << ", expected 0x" << BufferHeader::MAGIC
                  << std::dec << ")\n";
        nixl_.deregister_buffer();
        free_buffer(test_buffer_, test_buffer_size_);
        test_buffer_ = nullptr;
        nixl_.shutdown();
        return false;
    }

    // Store offsets
    ctrl_agent_slots_offset_ = header.agent_slots_offset;
    ctrl_notification_offset_ = header.notification_offset;

    initialized_ = true;
    return true;
}

bool Agent::register_with_controller() {
    if (!initialized_) {
        return false;
    }

    // Get our metadata blob
    std::string metadata = nixl_.get_metadata_blob();
    if (metadata.empty()) {
        return false;
    }

    if (metadata.size() > MAX_METADATA_BLOB_SIZE) {
        return false;
    }

    // Prepare AgentSlot data in wire format (big-endian for cross-platform RDMA)
    // Use registered test_buffer as source for RDMA write
    // AgentSlot structure: populated_flag (8) + metadata_size (4) + reserved (4) + metadata blob
    static_assert(AGENT_SLOT_SIZE <= DEFAULT_TEST_BUFFER_SIZE,
                  "AgentSlot must fit in test buffer");

    // Clear the slot area in our registered buffer
    std::memset(test_buffer_, 0, AGENT_SLOT_SIZE);

    uint8_t* slot_data = static_cast<uint8_t*>(test_buffer_);

    // Set populated_flag = 1 (in wire format)
    uint64_t populated_flag = to_wire64(1);
    std::memcpy(slot_data, &populated_flag, sizeof(populated_flag));

    // Set metadata_size (in wire format)
    uint32_t metadata_size_wire = to_wire32(static_cast<uint32_t>(metadata.size()));
    std::memcpy(slot_data + 8, &metadata_size_wire, sizeof(metadata_size_wire));

    // Copy metadata blob (offset 16 = after AgentSlot header)
    // Note: metadata blob is opaque bytes from NIXL, not converted
    std::memcpy(slot_data + AGENT_SLOT_HEADER_SIZE, metadata.data(), metadata.size());

    // Compute absolute slot address in controller buffer
    uintptr_t slot_addr = ctrl_buffer_base_addr_ + ctrl_agent_slots_offset_ + (agent_id_ * AGENT_SLOT_SIZE);

    // Write to controller buffer with notification (from registered memory)
    if (!nixl_.write_to_remote(controller_name_, slot_data, slot_addr,
                                AGENT_SLOT_SIZE, NOTIF_AGENT_REGISTERED)) {
        return false;
    }

    return true;
}

bool Agent::wait_for_rendezvous(std::chrono::milliseconds timeout) {
    if (!initialized_ || rendezvous_complete_) {
        return rendezvous_complete_;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        // Check for notifications
        auto notifications = nixl_.get_notifications();

        for (const auto& [sender, msg] : notifications) {
            if (sender == controller_name_ && msg == NOTIF_RENDEZVOUS_COMPLETE) {
                rendezvous_complete_ = true;
                return true;
            }
        }

        // Sleep briefly between checks
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

void Agent::shutdown() {
    if (!initialized_) {
        return;
    }

    nixl_.shutdown();

    if (test_buffer_ != nullptr) {
        free_buffer(test_buffer_, test_buffer_size_);
        test_buffer_ = nullptr;
    }

    test_buffer_size_ = 0;
    agent_id_ = 0;
    num_agents_ = 0;
    controller_name_.clear();
    ctrl_buffer_base_addr_ = 0;
    ctrl_agent_slots_offset_ = 0;
    ctrl_notification_offset_ = 0;
    initialized_ = false;
    rendezvous_complete_ = false;
}

} // namespace nixl_topo
