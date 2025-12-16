#pragma once

#include <string>
#include <memory>
#include <map>
#include <cstdint>

namespace nixl_topo {

/// Unified wrapper around NIXL API for both controller and agent use.
/// Handles initialization, memory registration, transfers, and notifications.
class NixlWrapper {
public:
    NixlWrapper();
    ~NixlWrapper();

    // Disable copy
    NixlWrapper(const NixlWrapper&) = delete;
    NixlWrapper& operator=(const NixlWrapper&) = delete;

    /// Initialize NIXL agent with UCX backend and progress thread.
    /// @param agent_name Unique name for this agent (e.g., "controller", "agent_0")
    /// @return true on success
    bool initialize(const std::string& agent_name);

    /// Check if initialized.
    bool is_initialized() const { return initialized_; }

    /// Register a memory buffer with NIXL.
    /// @param buffer Pointer to buffer
    /// @param size Size in bytes
    /// @return true on success
    bool register_buffer(void* buffer, size_t size);

    /// Deregister the previously registered buffer.
    bool deregister_buffer();

    /// Get the registered buffer's base address.
    /// @return Buffer address, or 0 if no buffer registered
    uintptr_t get_buffer_addr() const;

    /// Get the registered buffer's size.
    /// @return Buffer size, or 0 if no buffer registered
    size_t get_buffer_size() const;

    // =========================================================================
    // Metadata
    // =========================================================================

    /// Get serialized metadata blob (raw bytes).
    /// @return Metadata blob, or empty string on error
    std::string get_metadata_blob() const;

    /// Get serialized metadata as base64 (for environment variables).
    /// @return Base64-encoded metadata, or empty string on error
    std::string get_metadata_base64() const;

    /// Load remote agent's metadata from base64.
    /// @param base64_metadata Base64-encoded metadata blob
    /// @param agent_name Output: the remote agent's name
    /// @return true on success
    bool load_remote_metadata(const std::string& base64_metadata, std::string& agent_name);

    /// Load remote agent's metadata from raw blob.
    /// @param metadata Raw metadata blob
    /// @param agent_name Output: the remote agent's name
    /// @return true on success
    bool load_remote_metadata_blob(const std::string& metadata, std::string& agent_name);

    // =========================================================================
    // Transfers
    // =========================================================================

    /// Read data from remote agent's buffer.
    /// @param remote_agent Remote agent name
    /// @param local_buffer Local buffer to read into
    /// @param remote_addr Absolute address in remote agent's memory space
    /// @param size Number of bytes to read
    /// @return true on success
    bool read_from_remote(const std::string& remote_agent,
                          void* local_buffer,
                          uintptr_t remote_addr,
                          size_t size);

    /// Write data to remote agent's buffer.
    /// @param remote_agent Remote agent name
    /// @param local_buffer Local buffer to write from
    /// @param remote_addr Absolute address in remote agent's memory space
    /// @param size Number of bytes to write
    /// @param notify_msg Optional notification message (empty = no notification)
    /// @return true on success
    bool write_to_remote(const std::string& remote_agent,
                         const void* local_buffer,
                         uintptr_t remote_addr,
                         size_t size,
                         const std::string& notify_msg = "");

    // =========================================================================
    // Notifications
    // =========================================================================

    /// Get pending notifications from remote agents.
    /// @return Map of agent_name -> notification message
    std::map<std::string, std::string> get_notifications();

    /// Send notification to a remote agent.
    /// @param agent_name Target agent name
    /// @param message Notification message
    /// @return true on success
    bool send_notification(const std::string& agent_name, const std::string& message);

    /// Shutdown NIXL and release resources.
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

} // namespace nixl_topo
