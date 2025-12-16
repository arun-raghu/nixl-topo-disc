#include "controller.hpp"
#include "../common/types.hpp"
#include <thread>
#include <chrono>
#include <cstring>

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
            nixl_.send_notification(loaded_agent_names_[i], NOTIF_RENDEZVOUS_COMPLETE);
        }
    }
}

void Controller::shutdown() {
    if (!initialized_) {
        return;
    }

    nixl_.shutdown();
    buffer_.deallocate();
    num_agents_ = 0;
    initialized_ = false;
    loaded_agent_names_.clear();
    agent_metadata_loaded_.clear();
}

bool Controller::load_agent_metadata(uint32_t agent_id) {
    if (!initialized_ || agent_id >= num_agents_) {
        return false;
    }

    if (agent_metadata_loaded_[agent_id]) {
        return true;  // Already loaded
    }

    // Get agent's slot and convert from wire format
    const auto* wire_slot = buffer_.agent_slot(agent_id);
    if (!wire_slot) {
        return false;
    }

    // Convert slot header from wire format (big-endian) to host format
    AgentSlot slot = *wire_slot;  // Copy to convert
    slot.from_wire();

    if (slot.populated_flag == 0) {
        return false;
    }

    // Extract metadata blob from the slot
    if (slot.metadata_size == 0 || slot.metadata_size > MAX_METADATA_BLOB_SIZE) {
        return false;
    }

    // Metadata blob follows the wire_slot header (use original pointer for data access)
    std::string metadata(reinterpret_cast<const char*>(wire_slot->metadata()), slot.metadata_size);

    // Load the remote agent's metadata
    std::string agent_name;
    if (!nixl_.load_remote_metadata_blob(metadata, agent_name)) {
        return false;
    }

    loaded_agent_names_[agent_id] = agent_name;
    agent_metadata_loaded_[agent_id] = true;
    return true;
}

} // namespace nixl_topo
