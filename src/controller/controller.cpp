#include "controller.hpp"
#include "../common/types.hpp"
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>

namespace nixl_topo {

Controller::Controller() = default;

Controller::~Controller() {
    shutdown();
}

bool Controller::initialize(uint32_t num_agents) {
    if (initialized_) {
        return false;
    }

    if (num_agents == 0) {
        return false;
    }

    // Initialize NIXL
    if (!nixl_.initialize("controller")) {
        return false;
    }

    // Allocate buffer
    if (!buffer_.allocate(num_agents)) {
        nixl_.shutdown();
        return false;
    }

    // Register buffer with NIXL
    if (!nixl_.register_buffer(buffer_.data(), buffer_.size())) {
        buffer_.deallocate();
        nixl_.shutdown();
        return false;
    }

    num_agents_ = num_agents;
    loaded_agent_names_.resize(num_agents);
    agent_metadata_loaded_.resize(num_agents, false);
    initialized_ = true;
    return true;
}

std::map<std::string, std::string> Controller::get_agent_env_vars(uint32_t agent_id) const {
    std::map<std::string, std::string> env_vars;

    if (!initialized_ || agent_id >= num_agents_) {
        return env_vars;
    }

    // Get base64-encoded NIXL metadata (contains endpoint + buffer descriptor)
    std::string metadata = nixl_.get_metadata_base64();
    if (metadata.empty()) {
        return env_vars;
    }

    env_vars["CTRL_METADATA"] = metadata;
    // Use big-endian (network byte order) for cross-platform compatibility
    env_vars["CTRL_BUFFER_ADDR"] = std::to_string(to_wire64(reinterpret_cast<uintptr_t>(buffer_.data())));
    env_vars["AGENT_ID"] = std::to_string(agent_id);
    env_vars["NUM_AGENTS"] = std::to_string(num_agents_);

    return env_vars;
}

bool Controller::wait_for_agent(uint32_t agent_id, std::chrono::milliseconds timeout) {
    if (!initialized_ || agent_id >= num_agents_) {
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (buffer_.is_agent_registered(agent_id)) {
            // Load agent's metadata so we can send it notifications
            if (!agent_metadata_loaded_[agent_id]) {
                load_agent_metadata(agent_id);
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool Controller::wait_for_all_agents(std::chrono::milliseconds timeout) {
    if (!initialized_) {
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_registered = true;
        for (uint32_t i = 0; i < num_agents_; ++i) {
            if (!buffer_.is_agent_registered(i)) {
                all_registered = false;
            } else if (!agent_metadata_loaded_[i]) {
                // Load newly registered agent's metadata
                load_agent_metadata(i);
            }
        }

        if (all_registered) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

void Controller::signal_rendezvous_complete() {
    if (!initialized_) {
        return;
    }

    // Set ready flag in header
    buffer_.set_ready_flag();

    // Send NIXL notification to each agent
    for (uint32_t i = 0; i < num_agents_; ++i) {
        if (agent_metadata_loaded_[i] && !loaded_agent_names_[i].empty()) {
            std::cout << "  Sending RENDEZVOUS_COMPLETE to agent " << i
                      << " (" << loaded_agent_names_[i] << ")...\n";
            bool sent = nixl_.send_notification(loaded_agent_names_[i], NOTIF_RENDEZVOUS_COMPLETE);
            if (!sent) {
                std::cerr << "  WARNING: Failed to send notification to " << loaded_agent_names_[i] << "\n";
            }
        } else {
            std::cerr << "  WARNING: Agent " << i << " metadata not loaded (loaded="
                      << agent_metadata_loaded_[i] << ", name='"
                      << (i < loaded_agent_names_.size() ? loaded_agent_names_[i] : "")
                      << "')\n";
        }
    }
}

bool Controller::wait_for_peer_discovery(std::chrono::milliseconds timeout) {
    if (!initialized_) {
        return false;
    }

    std::cout << "Controller: Waiting for peer discovery from all agents...\n";

    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<bool> peer_discovery_done(num_agents_, false);
    uint32_t done_count = 0;

    while (std::chrono::steady_clock::now() < deadline && done_count < num_agents_) {
        // Check for notifications
        auto notifications = nixl_.get_notifications();

        for (const auto& [sender, msg] : notifications) {
            if (msg == NOTIF_PEER_DISCOVERY_COMPLETE) {
                // Find which agent sent this
                for (uint32_t i = 0; i < num_agents_; ++i) {
                    if (loaded_agent_names_[i] == sender && !peer_discovery_done[i]) {
                        peer_discovery_done[i] = true;
                        done_count++;
                        std::cout << "Controller: Agent " << i << " (" << sender
                                  << ") completed peer discovery (" << done_count
                                  << "/" << num_agents_ << ")\n";
                        break;
                    }
                }
            }
        }

        if (done_count < num_agents_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (done_count == num_agents_) {
        std::cout << "Controller: All agents completed peer discovery!\n";
        return true;
    }

    std::cerr << "Controller: Timeout waiting for peer discovery\n";
    for (uint32_t i = 0; i < num_agents_; ++i) {
        if (!peer_discovery_done[i]) {
            std::cerr << "  Agent " << i << ": not done\n";
        }
    }
    return false;
}

void Controller::shutdown() {
    if (!initialized_) {
        return;
    }

    nixl_.shutdown();
    buffer_.deallocate();
    num_agents_ = 0;
    initialized_ = false;
    command_seq_ = 0;
    loaded_agent_names_.clear();
    agent_metadata_loaded_.clear();
}

bool Controller::issue_ping_pong_test(uint32_t initiator_id, uint32_t responder_id,
                                       uint64_t message_size, uint32_t iterations,
                                       uint32_t warmup_iterations) {
    // Validate
    if (!initialized_) return false;
    if (initiator_id >= num_agents_ || responder_id >= num_agents_) return false;
    if (initiator_id == responder_id) return false;
    if (message_size > MAILBOX_SIZE - sizeof(MailboxHeader)) return false;

    // Ensure agent metadata is loaded
    if (!agent_metadata_loaded_[initiator_id] || !agent_metadata_loaded_[responder_id]) {
        return false;
    }

    ++command_seq_;

    // Get peer buffer addresses from agent slots
    const auto* initiator_slot = buffer_.agent_slot(initiator_id);
    const auto* responder_slot = buffer_.agent_slot(responder_id);
    if (!initiator_slot || !responder_slot) return false;

    uint64_t initiator_buf = from_wire64(initiator_slot->buffer_base_addr);
    uint64_t responder_buf = from_wire64(responder_slot->buffer_base_addr);

    std::cout << "Controller: issue_ping_pong_test debug:\n"
              << "  initiator_id=" << initiator_id << " name='" << loaded_agent_names_[initiator_id] << "'\n"
              << "  responder_id=" << responder_id << " name='" << loaded_agent_names_[responder_id] << "'\n"
              << "  initiator_buf=0x" << std::hex << initiator_buf << "\n"
              << "  responder_buf=0x" << std::hex << responder_buf << std::dec << "\n";

    // Use registered command area for RDMA writes (stack memory won't work for RDMA)
    uint8_t* cmd_buf = static_cast<uint8_t*>(buffer_.cmd_slot(0));

    // Build command for responder (send first)
    TestCommand resp_cmd = {};
    resp_cmd.command_seq = command_seq_;
    resp_cmd.command_type = CommandType::PING_PONG_LATENCY;
    resp_cmd.role = TestRole::RESPONDER;
    resp_cmd.peer_agent_id = initiator_id;
    resp_cmd.message_size = message_size;
    resp_cmd.iterations = iterations;
    resp_cmd.warmup_iterations = warmup_iterations;
    resp_cmd.to_wire();

    // Copy to registered buffer for RDMA
    std::memcpy(cmd_buf, &resp_cmd, sizeof(resp_cmd));

    // RDMA write to responder's command inbox (responder gets command first)
    uint64_t resp_inbox_addr = responder_buf + AGENT_COMMAND_INBOX_OFFSET;
    std::cout << "  Writing to responder inbox at 0x" << std::hex << resp_inbox_addr << std::dec << "\n";
    if (!nixl_.write_to_remote(loaded_agent_names_[responder_id],
                               cmd_buf, resp_inbox_addr, sizeof(resp_cmd))) {
        std::cerr << "Controller: Failed to write command to responder " << responder_id << "\n";
        return false;
    }

    // Small delay to ensure responder is ready before initiator starts
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Build command for initiator
    TestCommand init_cmd = {};
    init_cmd.command_seq = command_seq_;
    init_cmd.command_type = CommandType::PING_PONG_LATENCY;
    init_cmd.role = TestRole::INITIATOR;
    init_cmd.peer_agent_id = responder_id;
    init_cmd.message_size = message_size;
    init_cmd.iterations = iterations;
    init_cmd.warmup_iterations = warmup_iterations;
    init_cmd.to_wire();

    // Copy to registered buffer for RDMA
    std::memcpy(cmd_buf, &init_cmd, sizeof(init_cmd));

    // RDMA write to initiator's command inbox
    uint64_t init_inbox_addr = initiator_buf + AGENT_COMMAND_INBOX_OFFSET;
    std::cout << "  Writing to initiator inbox at 0x" << std::hex << init_inbox_addr << std::dec << "\n";
    if (!nixl_.write_to_remote(loaded_agent_names_[initiator_id],
                               cmd_buf, init_inbox_addr, sizeof(init_cmd))) {
        std::cerr << "Controller: Failed to write command to initiator " << initiator_id << "\n";
        return false;
    }

    std::cout << "Controller: Issued ping-pong test (seq=" << command_seq_
              << ") initiator=" << initiator_id << " responder=" << responder_id
              << " msg_size=" << message_size << " iterations=" << iterations << "\n";

    return true;
}

bool Controller::shutdown_agents() {
    if (!initialized_) return false;

    std::cout << "Controller: Sending SHUTDOWN to all agents...\n";

    ++command_seq_;

    // Use registered command area for RDMA writes
    uint8_t* cmd_buf = static_cast<uint8_t*>(buffer_.cmd_slot(0));

    bool all_success = true;
    for (uint32_t agent_id = 0; agent_id < num_agents_; ++agent_id) {
        if (!agent_metadata_loaded_[agent_id]) {
            std::cerr << "Controller: Agent " << agent_id << " metadata not loaded, skipping\n";
            continue;
        }

        // Get agent's buffer address
        const auto* slot = buffer_.agent_slot(agent_id);
        if (!slot) continue;

        uint64_t agent_buf = from_wire64(slot->buffer_base_addr);
        uint64_t inbox_addr = agent_buf + AGENT_COMMAND_INBOX_OFFSET;

        // Build SHUTDOWN command
        TestCommand cmd = {};
        cmd.command_seq = command_seq_;
        cmd.command_type = CommandType::SHUTDOWN;
        cmd.to_wire();

        // Copy to registered buffer and send
        std::memcpy(cmd_buf, &cmd, sizeof(cmd));

        if (!nixl_.write_to_remote(loaded_agent_names_[agent_id],
                                   cmd_buf, inbox_addr, sizeof(cmd))) {
            std::cerr << "Controller: Failed to send SHUTDOWN to agent " << agent_id << "\n";
            all_success = false;
        } else {
            std::cout << "Controller: Sent SHUTDOWN to agent " << agent_id << "\n";
        }
    }

    return all_success;
}

bool Controller::wait_for_results(const std::vector<uint32_t>& agent_ids,
                                   uint64_t expected_seq,
                                   std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_complete = true;
        for (uint32_t id : agent_ids) {
            const auto* result = buffer_.result_slot(id);
            if (!result) return false;

            // Check result in wire format
            uint64_t seq = from_wire64(result->command_seq);
            auto status = static_cast<TestStatus>(from_wire32(static_cast<uint32_t>(result->status)));

            if (seq != expected_seq || status == TestStatus::PENDING || status == TestStatus::RUNNING) {
                all_complete = false;
                break;
            }
        }
        if (all_complete) {
            // Print results received from each agent
            for (uint32_t id : agent_ids) {
                const auto* result = buffer_.result_slot(id);
                if (result) {
                    uint64_t seq = from_wire64(result->command_seq);
                    auto status = static_cast<TestStatus>(from_wire32(static_cast<uint32_t>(result->status)));
                    uint32_t agent_id = from_wire32(result->agent_id);
                    uint64_t iterations = from_wire64(result->iterations_completed);
                    uint64_t min_lat = from_wire64(result->min_latency_ns);
                    uint64_t max_lat = from_wire64(result->max_latency_ns);
                    uint64_t avg_lat = from_wire64(result->avg_latency_ns);
                    uint64_t total_bytes = from_wire64(result->total_bytes);

                    std::cout << "Controller: Results received from agent " << id << ":\n"
                              << "  command_seq=" << seq << "\n"
                              << "  status=" << static_cast<int>(status)
                              << " (" << (status == TestStatus::COMPLETE ? "COMPLETE" :
                                          status == TestStatus::ERROR ? "ERROR" : "OTHER") << ")\n"
                              << "  agent_id=" << agent_id << "\n"
                              << "  iterations_completed=" << iterations << "\n"
                              << "  min_latency_ns=" << min_lat << "\n"
                              << "  max_latency_ns=" << max_lat << "\n"
                              << "  avg_latency_ns=" << avg_lat << "\n"
                              << "  total_bytes=" << total_bytes << "\n";
                }
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

const TestResult* Controller::get_result(uint32_t agent_id) const {
    return buffer_.result_slot(agent_id);
}

bool Controller::load_agent_metadata(uint32_t agent_id) {
    if (!initialized_ || agent_id >= num_agents_) {
        std::cerr << "Controller::load_agent_metadata: invalid state (init="
                  << initialized_ << ", agent_id=" << agent_id << ", num=" << num_agents_ << ")\n";
        return false;
    }

    if (agent_metadata_loaded_[agent_id]) {
        return true;  // Already loaded
    }

    // Get agent's slot and convert from wire format
    const auto* wire_slot = buffer_.agent_slot(agent_id);
    if (!wire_slot) {
        std::cerr << "Controller::load_agent_metadata: wire_slot is null for agent " << agent_id << "\n";
        return false;
    }

    // Convert slot header from wire format (big-endian) to host format
    AgentSlot slot = *wire_slot;  // Copy to convert
    slot.from_wire();

    if (slot.populated_flag == 0) {
        std::cerr << "Controller::load_agent_metadata: populated_flag is 0 for agent " << agent_id << "\n";
        return false;
    }

    // Extract metadata blob from the slot
    if (slot.metadata_size == 0 || slot.metadata_size > MAX_METADATA_BLOB_SIZE) {
        std::cerr << "Controller::load_agent_metadata: invalid metadata_size=" << slot.metadata_size
                  << " for agent " << agent_id << "\n";
        return false;
    }

    // Metadata blob follows the wire_slot header (use original pointer for data access)
    std::string metadata(reinterpret_cast<const char*>(wire_slot->metadata()), slot.metadata_size);

    // Load the remote agent's metadata
    std::string agent_name;
    if (!nixl_.load_remote_metadata_blob(metadata, agent_name)) {
        std::cerr << "Controller::load_agent_metadata: load_remote_metadata_blob failed for agent " << agent_id << "\n";
        return false;
    }

    std::cout << "Controller: Loaded metadata for agent " << agent_id
              << " -> '" << agent_name << "' (metadata_size=" << slot.metadata_size << ")\n";

    loaded_agent_names_[agent_id] = agent_name;
    agent_metadata_loaded_[agent_id] = true;
    return true;
}

} // namespace nixl_topo
