#include "agent.hpp"
#include "../common/types.hpp"
#include "../common/memory.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
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
    ctrl_result_offset_ = header.result_offset;

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
    // AgentSlot structure: populated_flag (8) + buffer_base_addr (8) + metadata_size (4) + reserved (4) + metadata blob
    static_assert(AGENT_SLOT_SIZE <= DEFAULT_TEST_BUFFER_SIZE,
                  "AgentSlot must fit in test buffer");

    // Clear the slot area in our registered buffer
    std::memset(test_buffer_, 0, AGENT_SLOT_SIZE);

    uint8_t* slot_data = static_cast<uint8_t*>(test_buffer_);

    // Set populated_flag = 1 (in wire format) - offset 0
    uint64_t populated_flag = to_wire64(1);
    std::memcpy(slot_data, &populated_flag, sizeof(populated_flag));

    // Set buffer_base_addr (in wire format) - offset 8
    uint64_t buffer_addr = to_wire64(reinterpret_cast<uintptr_t>(test_buffer_));
    std::memcpy(slot_data + 8, &buffer_addr, sizeof(buffer_addr));

    // Set metadata_size (in wire format) - offset 16
    uint32_t metadata_size_wire = to_wire32(static_cast<uint32_t>(metadata.size()));
    std::memcpy(slot_data + 16, &metadata_size_wire, sizeof(metadata_size_wire));

    // Copy metadata blob (offset 24 = after AgentSlot header)
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
    int poll_count = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        // Check for notifications
        auto notifications = nixl_.get_notifications();

        if (!notifications.empty()) {
            std::cout << "Agent " << agent_id_ << ": Received " << notifications.size() << " notification(s)\n";
        }

        for (const auto& [sender, msg] : notifications) {
            std::cout << "Agent " << agent_id_ << ": Notification from '" << sender
                      << "': '" << msg << "' (expecting '" << controller_name_
                      << "':'" << NOTIF_RENDEZVOUS_COMPLETE << "')\n";

            if (sender == controller_name_ && msg == NOTIF_RENDEZVOUS_COMPLETE) {
                rendezvous_complete_ = true;
                return true;
            }
        }

        // Sleep briefly between checks
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        poll_count++;

        // Log progress every 10 seconds
        if (poll_count % 100 == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - (deadline - timeout)).count();
            std::cout << "Agent " << agent_id_ << ": Still waiting for rendezvous... ("
                      << elapsed << "s elapsed)\n";
        }
    }

    return false;
}

bool Agent::discover_peers() {
    if (!rendezvous_complete_) {
        std::cerr << "Agent::discover_peers: rendezvous not complete\n";
        return false;
    }

    // Initialize peers vector
    peers_.resize(num_agents_);

    // Read all agent slots from controller in a single NIXL call
    // All slots are contiguous in the controller buffer
    size_t slots_size = num_agents_ * AGENT_SLOT_SIZE;
    uintptr_t slots_addr = ctrl_buffer_base_addr_ + ctrl_agent_slots_offset_;

    if (!nixl_.read_from_remote(controller_name_, test_buffer_, slots_addr, slots_size)) {
        std::cerr << "Agent::discover_peers: failed to read agent slots from controller\n";
        return false;
    }

    // Parse each peer's slot
    bool all_connected = true;
    for (uint32_t peer_id = 0; peer_id < num_agents_; ++peer_id) {
        if (peer_id == agent_id_) {
            // Skip self - but mark as connected with own info
            peers_[peer_id].nixl_name = "agent_" + std::to_string(peer_id);
            peers_[peer_id].buffer_addr = reinterpret_cast<uintptr_t>(test_buffer_);
            peers_[peer_id].connected = true;
            continue;
        }

        uint8_t* slot_data = static_cast<uint8_t*>(test_buffer_) + (peer_id * AGENT_SLOT_SIZE);

        // Parse AgentSlot header (in wire format)
        uint64_t populated_flag_wire;
        uint64_t buffer_addr_wire;
        uint32_t metadata_size_wire;

        std::memcpy(&populated_flag_wire, slot_data, sizeof(populated_flag_wire));
        std::memcpy(&buffer_addr_wire, slot_data + 8, sizeof(buffer_addr_wire));
        std::memcpy(&metadata_size_wire, slot_data + 16, sizeof(metadata_size_wire));

        uint64_t populated_flag = from_wire64(populated_flag_wire);
        uint64_t buffer_addr = from_wire64(buffer_addr_wire);
        uint32_t metadata_size = from_wire32(metadata_size_wire);

        if (populated_flag == 0) {
            std::cerr << "Agent::discover_peers: peer " << peer_id << " slot not populated\n";
            all_connected = false;
            continue;
        }

        if (metadata_size > MAX_METADATA_BLOB_SIZE) {
            std::cerr << "Agent::discover_peers: peer " << peer_id
                      << " metadata size " << metadata_size << " exceeds max\n";
            all_connected = false;
            continue;
        }

        // Extract metadata blob (offset AGENT_SLOT_HEADER_SIZE = 24)
        std::string metadata(reinterpret_cast<const char*>(slot_data + AGENT_SLOT_HEADER_SIZE),
                            metadata_size);

        // Load peer's NIXL metadata
        std::string peer_name;
        if (!nixl_.load_remote_metadata_blob(metadata, peer_name)) {
            std::cerr << "Agent::discover_peers: failed to load metadata for peer " << peer_id << "\n";
            all_connected = false;
            continue;
        }

        // Store peer info
        peers_[peer_id].nixl_name = peer_name;
        peers_[peer_id].buffer_addr = buffer_addr;
        peers_[peer_id].connected = true;

        std::cout << "Agent::discover_peers: connected to peer " << peer_id
                  << " (" << peer_name << ") at buffer 0x" << std::hex << buffer_addr << std::dec << "\n";
    }

    return all_connected;
}

bool Agent::notify_peer_discovery_complete() {
    if (!rendezvous_complete_) {
        std::cerr << "Agent::notify_peer_discovery_complete: rendezvous not complete\n";
        return false;
    }

    std::cout << "Agent " << agent_id_ << ": Notifying controller of peer discovery complete\n";

    if (!nixl_.send_notification(controller_name_, NOTIF_PEER_DISCOVERY_COMPLETE)) {
        std::cerr << "Agent " << agent_id_ << ": Failed to send peer discovery notification\n";
        return false;
    }

    return true;
}

bool Agent::write_to_peer(uint32_t peer_id, size_t offset, const void* data, size_t size) {
    if (peer_id >= peers_.size() || !peers_[peer_id].connected) {
        std::cerr << "Agent::write_to_peer: peer " << peer_id << " not connected\n";
        return false;
    }

    if (peer_id == agent_id_) {
        std::cerr << "Agent::write_to_peer: cannot write to self\n";
        return false;
    }

    uintptr_t remote_addr = peers_[peer_id].buffer_addr + offset;
    return nixl_.write_to_remote(peers_[peer_id].nixl_name, data, remote_addr, size);
}

bool Agent::read_from_peer(uint32_t peer_id, size_t offset, void* data, size_t size) {
    if (peer_id >= peers_.size() || !peers_[peer_id].connected) {
        std::cerr << "Agent::read_from_peer: peer " << peer_id << " not connected\n";
        return false;
    }

    if (peer_id == agent_id_) {
        std::cerr << "Agent::read_from_peer: cannot read from self\n";
        return false;
    }

    uintptr_t remote_addr = peers_[peer_id].buffer_addr + offset;
    return nixl_.read_from_remote(peers_[peer_id].nixl_name, data, remote_addr, size);
}

bool Agent::verify_peer_transfer(uint32_t peer_id, size_t size) {
    if (peer_id >= peers_.size() || !peers_[peer_id].connected) {
        std::cerr << "Agent::verify_peer_transfer: peer " << peer_id << " not connected\n";
        return false;
    }

    if (size > test_buffer_size_ / 2) {
        std::cerr << "Agent::verify_peer_transfer: size " << size << " too large\n";
        return false;
    }

    // Use first half of test_buffer for send pattern, second half for readback
    uint8_t* send_buf = static_cast<uint8_t*>(test_buffer_);
    uint8_t* recv_buf = static_cast<uint8_t*>(test_buffer_) + (test_buffer_size_ / 2);

    // Generate test pattern (deterministic based on agent_id and offset)
    for (size_t i = 0; i < size; ++i) {
        send_buf[i] = static_cast<uint8_t>((agent_id_ ^ i) & 0xFF);
    }

    // Print first 16 bytes of send data
    std::cout << "Agent " << agent_id_ << ": Writing " << size << " bytes to peer " << peer_id
              << " at 0x" << std::hex << peers_[peer_id].buffer_addr << std::dec << "\n";
    std::cout << "  Send data (first 16 bytes): ";
    for (size_t i = 0; i < std::min(size, size_t(16)); ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)send_buf[i] << " ";
    }
    std::cout << std::dec << "\n";

    // Write pattern to peer's buffer
    if (!write_to_peer(peer_id, 0, send_buf, size)) {
        std::cerr << "Agent::verify_peer_transfer: write_to_peer failed\n";
        return false;
    }
    std::cout << "  Write completed\n";

    // Clear recv buffer before read
    std::memset(recv_buf, 0, size);

    // Read back from peer's buffer
    if (!read_from_peer(peer_id, 0, recv_buf, size)) {
        std::cerr << "Agent::verify_peer_transfer: read_from_peer failed\n";
        return false;
    }

    // Print first 16 bytes of received data
    std::cout << "  Recv data (first 16 bytes): ";
    for (size_t i = 0; i < std::min(size, size_t(16)); ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)recv_buf[i] << " ";
    }
    std::cout << std::dec << "\n";

    // Compare
    if (std::memcmp(send_buf, recv_buf, size) != 0) {
        std::cerr << "Agent::verify_peer_transfer: data mismatch\n";
        // Find first mismatch for debugging
        for (size_t i = 0; i < size; ++i) {
            if (send_buf[i] != recv_buf[i]) {
                std::cerr << "  First mismatch at offset " << i
                          << ": sent 0x" << std::hex << (int)send_buf[i]
                          << ", recv 0x" << (int)recv_buf[i] << std::dec << "\n";
                break;
            }
        }
        return false;
    }

    return true;
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
    ctrl_result_offset_ = 0;
    last_command_seq_ = 0;
    initialized_ = false;
    rendezvous_complete_ = false;
}

bool Agent::poll_and_execute_command() {
    if (!rendezvous_complete_) return false;

    // Get pointer to command inbox in our buffer
    uint8_t* inbox = static_cast<uint8_t*>(test_buffer_) + AGENT_COMMAND_INBOX_OFFSET;

    // Read command (already in our local memory, written by controller via RDMA)
    TestCommand cmd;
    std::memcpy(&cmd, inbox, sizeof(cmd));
    cmd.from_wire();

    // Check if new command
    if (cmd.command_seq == 0 || cmd.command_seq <= last_command_seq_) {
        return false;  // No new command
    }

    auto cmd_type_str = [](CommandType t) {
        switch (t) {
            case CommandType::NONE: return "NONE";
            case CommandType::SHUTDOWN: return "SHUTDOWN";
            case CommandType::PING_PONG_LATENCY: return "PING_PONG_LATENCY";
            default: return "UNKNOWN";
        }
    };

    std::cout << "Agent " << agent_id_ << ": Received command:\n"
              << "  seq=" << cmd.command_seq << "\n"
              << "  type=" << static_cast<int>(cmd.command_type)
              << " (" << cmd_type_str(cmd.command_type) << ")\n"
              << "  role=" << static_cast<int>(cmd.role)
              << " (" << (cmd.role == TestRole::INITIATOR ? "INITIATOR" : "RESPONDER") << ")\n"
              << "  peer_agent_id=" << cmd.peer_agent_id << "\n"
              << "  message_size=" << cmd.message_size << "\n"
              << "  iterations=" << cmd.iterations << "\n"
              << "  warmup_iterations=" << cmd.warmup_iterations << "\n";

    last_command_seq_ = cmd.command_seq;

    // Execute based on command type
    if (cmd.command_type == CommandType::SHUTDOWN) {
        std::cout << "Agent " << agent_id_ << ": Received SHUTDOWN command\n";
        shutdown_requested_ = true;
        return true;
    }

    if (cmd.command_type == CommandType::PING_PONG_LATENCY) {
        TestResult result = execute_ping_pong(cmd);

        // Only initiator reports result
        if (cmd.role == TestRole::INITIATOR) {
            report_result(result);
        }
        return true;
    }

    return false;
}

TestResult Agent::execute_ping_pong(const TestCommand& cmd) {
    TestResult result = {};
    result.command_seq = cmd.command_seq;
    result.agent_id = agent_id_;
    result.status = TestStatus::RUNNING;

    uint32_t total_iterations = cmd.warmup_iterations + cmd.iterations;
    uint64_t peer_mailbox_addr = peers_[cmd.peer_agent_id].buffer_addr + AGENT_MAILBOX_OFFSET;

    // Our mailbox for receiving
    uint8_t* my_mailbox = static_cast<uint8_t*>(test_buffer_) + AGENT_MAILBOX_OFFSET;
    MailboxHeader* my_header = reinterpret_cast<MailboxHeader*>(my_mailbox);

    // Send buffer (use beginning of test_buffer_)
    uint8_t* send_buf = static_cast<uint8_t*>(test_buffer_);
    MailboxHeader* send_header = reinterpret_cast<MailboxHeader*>(send_buf);

    // Initialize send buffer with payload
    size_t total_msg_size = sizeof(MailboxHeader) + cmd.message_size;
    std::memset(send_buf, 0, total_msg_size);

    // Reset mailbox sequence for this test (so we don't see stale sequences from previous tests)
    my_header->sequence = 0;

    // Latency tracking (for initiator)
    std::vector<uint64_t> latencies;
    if (cmd.role == TestRole::INITIATOR) {
        latencies.reserve(cmd.iterations);
    }

    uint64_t my_last_seen_seq = 0;  // Start fresh for this test

    // Timeout for waiting on peer response (5 seconds)
    constexpr auto POLL_TIMEOUT = std::chrono::seconds(5);

    std::cout << "Agent " << agent_id_ << ": Starting ping-pong test"
              << " role=" << (cmd.role == TestRole::INITIATOR ? "INITIATOR" : "RESPONDER")
              << " peer=" << cmd.peer_agent_id
              << " iterations=" << cmd.iterations
              << " warmup=" << cmd.warmup_iterations
              << " msg_size=" << cmd.message_size << "\n";

    if (cmd.role == TestRole::INITIATOR) {
        // INITIATOR: send ping, wait for pong
        for (uint32_t i = 0; i < total_iterations; ++i) {
            bool is_warmup = (i < cmd.warmup_iterations);

            // Prepare ping
            send_header->sequence = i + 1;
            send_header->timestamp_ns = 0;

            auto start = std::chrono::steady_clock::now();

            // Send ping to responder's mailbox
            if (!nixl_.write_to_remote(peers_[cmd.peer_agent_id].nixl_name,
                                       send_buf, peer_mailbox_addr, total_msg_size)) {
                result.status = TestStatus::ERROR;
                result.error_code = 1;  // Write failed
                std::cerr << "Agent " << agent_id_ << ": Ping write failed at iteration " << i << "\n";
                return result;
            }

            // Poll for pong in our mailbox (with timeout)
            auto poll_start = std::chrono::steady_clock::now();
            while (my_header->sequence <= my_last_seen_seq) {
                if (std::chrono::steady_clock::now() - poll_start > POLL_TIMEOUT) {
                    result.status = TestStatus::ERROR;
                    result.error_code = 2;  // Timeout waiting for pong
                    std::cerr << "Agent " << agent_id_ << ": Timeout waiting for pong at iteration " << i
                              << " (expected seq > " << my_last_seen_seq << ", got " << my_header->sequence << ")\n";
                    return result;
                }
            }
            my_last_seen_seq = my_header->sequence;

            auto end = std::chrono::steady_clock::now();

            if (!is_warmup) {
                uint64_t rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                latencies.push_back(rtt_ns);
            }
        }

        // Calculate statistics
        result.iterations_completed = latencies.size();
        result.total_bytes = cmd.message_size * cmd.iterations * 2;  // ping + pong

        if (!latencies.empty()) {
            result.min_latency_ns = *std::min_element(latencies.begin(), latencies.end());
            result.max_latency_ns = *std::max_element(latencies.begin(), latencies.end());
            uint64_t sum = 0;
            for (auto lat : latencies) sum += lat;
            result.avg_latency_ns = sum / latencies.size();
        }
        result.status = TestStatus::COMPLETE;

        std::cout << "Agent " << agent_id_ << ": Ping-pong test complete"
                  << " min=" << result.min_latency_ns << "ns"
                  << " max=" << result.max_latency_ns << "ns"
                  << " avg=" << result.avg_latency_ns << "ns\n";

    } else {
        // RESPONDER: wait for ping, send pong
        for (uint32_t i = 0; i < total_iterations; ++i) {
            // Poll for ping in our mailbox (with timeout)
            auto poll_start = std::chrono::steady_clock::now();
            while (my_header->sequence <= my_last_seen_seq) {
                if (std::chrono::steady_clock::now() - poll_start > POLL_TIMEOUT) {
                    result.status = TestStatus::ERROR;
                    result.error_code = 3;  // Timeout waiting for ping
                    std::cerr << "Agent " << agent_id_ << ": Timeout waiting for ping at iteration " << i
                              << " (expected seq > " << my_last_seen_seq << ", got " << my_header->sequence << ")\n";
                    return result;
                }
            }
            my_last_seen_seq = my_header->sequence;

            // Send pong to initiator's mailbox
            send_header->sequence = i + 1;
            if (!nixl_.write_to_remote(peers_[cmd.peer_agent_id].nixl_name,
                                       send_buf, peer_mailbox_addr, total_msg_size)) {
                // Log error but continue
                std::cerr << "Agent " << agent_id_ << ": Pong write failed at iteration " << i << "\n";
            }
        }
        result.status = TestStatus::COMPLETE;
        result.iterations_completed = total_iterations;

        std::cout << "Agent " << agent_id_ << ": Responder completed " << total_iterations << " iterations\n";
    }

    return result;
}

bool Agent::report_result(const TestResult& result) {
    // Compute result slot address in controller buffer
    uintptr_t result_addr = ctrl_buffer_base_addr_ + ctrl_result_offset_ +
                            (agent_id_ * TEST_RESULT_SIZE);

    // Prepare result in wire format
    TestResult wire_result = result;
    wire_result.to_wire();

    // Use registered buffer for RDMA write
    std::memcpy(test_buffer_, &wire_result, sizeof(wire_result));

    if (!nixl_.write_to_remote(controller_name_, test_buffer_, result_addr, sizeof(TestResult))) {
        std::cerr << "Agent " << agent_id_ << ": Failed to write result to controller\n";
        return false;
    }

    std::cout << "Agent " << agent_id_ << ": Reported result (seq=" << result.command_seq
              << ", status=" << static_cast<int>(result.status)
              << ", avg_latency=" << result.avg_latency_ns << "ns)\n";
    return true;
}

void Agent::run_command_loop(volatile std::sig_atomic_t& shutdown_flag) {
    std::cout << "Agent " << agent_id_ << ": Entering command loop\n";

    while (!shutdown_flag && !shutdown_requested_) {
        if (poll_and_execute_command()) {
            // Command executed, check for shutdown
            if (shutdown_requested_) {
                break;
            }
            // Check again immediately for more commands
            continue;
        }
        // No command, sleep briefly
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    if (shutdown_requested_) {
        std::cout << "Agent " << agent_id_ << ": Exiting command loop (controller shutdown)\n";
    } else {
        std::cout << "Agent " << agent_id_ << ": Exiting command loop (signal)\n";
    }
}

} // namespace nixl_topo
