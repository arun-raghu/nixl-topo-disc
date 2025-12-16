#include "nixl_wrapper.hpp"
#include <nixl.h>
#include <nixl_descriptors.h>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <iostream>

namespace nixl_topo {

// =============================================================================
// Base64 encoding/decoding
// =============================================================================

namespace {

// URL-safe base64 alphabet (uses - and _ instead of + and / to avoid shell issues)
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64_encode(const std::string& input) {
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    uint32_t val = 0;
    int valb = -6;

    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        output.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (output.size() % 4) {
        output.push_back('=');
    }

    return output;
}

std::string base64_decode(const std::string& input) {
    // URL-safe base64 decode table (- at 45 = 62, _ at 95 = 63)
    static const int decode_table[] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1
    };

    std::string output;
    output.reserve((input.size() / 4) * 3);

    uint32_t val = 0;
    int valb = -8;

    for (unsigned char c : input) {
        if (c == '=') break;
        if (c >= 128 || decode_table[c] == -1) continue;

        val = (val << 6) + decode_table[c];
        valb += 6;

        if (valb >= 0) {
            output.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return output;
}

} // anonymous namespace

// =============================================================================
// NixlWrapper Implementation
// =============================================================================

class NixlWrapper::Impl {
public:
    std::unique_ptr<nixlAgent> agent;
    nixlBackendH* ucx_backend = nullptr;
    nixl_reg_dlist_t registered_descs{DRAM_SEG};
    void* registered_buffer = nullptr;
    size_t registered_size = 0;
};

NixlWrapper::NixlWrapper() : impl_(std::make_unique<Impl>()) {}

NixlWrapper::~NixlWrapper() {
    shutdown();
}

bool NixlWrapper::initialize(const std::string& agent_name) {
    if (initialized_) {
        return false;
    }

    try {
        // Create agent config with progress thread for async operations
        nixlAgentConfig config(true);  // enable progress thread

        // Create NIXL agent
        impl_->agent = std::make_unique<nixlAgent>(agent_name, config);

        // Get available plugins
        std::vector<nixl_backend_t> plugins;
        auto status = impl_->agent->getAvailPlugins(plugins);
        if (status != NIXL_SUCCESS) {
            return false;
        }

        // Check if UCX is available
        bool ucx_found = false;
        for (const auto& plugin : plugins) {
            if (plugin == "UCX") {
                ucx_found = true;
                break;
            }
        }
        if (!ucx_found) {
            return false;
        }

        // Get UCX plugin params (required before createBackend)
        nixl_b_params_t ucx_params;
        nixl_mem_list_t mem_list;
        status = impl_->agent->getPluginParams("UCX", mem_list, ucx_params);
        if (status != NIXL_SUCCESS) {
            return false;
        }

        // Configure UCX to use TCP transport for container compatibility
        // This avoids shared memory issues when running in containers
        ucx_params["engine_config"] = "TLS=tcp";

        // Create UCX backend with configured params
        status = impl_->agent->createBackend("UCX", ucx_params, impl_->ucx_backend);
        if (status != NIXL_SUCCESS) {
            return false;
        }

        initialized_ = true;
        return true;

    } catch (const std::exception&) {
        return false;
    }
}

bool NixlWrapper::register_buffer(void* buffer, size_t size) {
    if (!initialized_ || !buffer || size == 0) {
        return false;
    }

    if (impl_->registered_buffer != nullptr) {
        return false;  // Already registered
    }

    try {
        // Create descriptor for the buffer
        nixlBlobDesc desc;
        desc.addr = reinterpret_cast<uintptr_t>(buffer);
        desc.len = size;
        desc.devId = 0;

        impl_->registered_descs.addDesc(desc);

        // Pass backend in extra_params (required for proper memory registration)
        nixl_opt_args_t extra_params;
        extra_params.backends.push_back(impl_->ucx_backend);

        // Register with NIXL
        auto status = impl_->agent->registerMem(impl_->registered_descs, &extra_params);
        if (status != NIXL_SUCCESS) {
            impl_->registered_descs.clear();
            return false;
        }

        impl_->registered_buffer = buffer;
        impl_->registered_size = size;
        return true;

    } catch (const std::exception&) {
        return false;
    }
}

bool NixlWrapper::deregister_buffer() {
    if (!initialized_ || impl_->registered_buffer == nullptr) {
        return false;
    }

    try {
        // Pass backend in extra_params (must match registration)
        nixl_opt_args_t extra_params;
        extra_params.backends.push_back(impl_->ucx_backend);

        auto status = impl_->agent->deregisterMem(impl_->registered_descs, &extra_params);
        impl_->registered_descs.clear();
        impl_->registered_buffer = nullptr;
        impl_->registered_size = 0;
        return status == NIXL_SUCCESS;

    } catch (const std::exception&) {
        return false;
    }
}

uintptr_t NixlWrapper::get_buffer_addr() const {
    return reinterpret_cast<uintptr_t>(impl_->registered_buffer);
}

size_t NixlWrapper::get_buffer_size() const {
    return impl_->registered_size;
}

std::string NixlWrapper::get_metadata_blob() const {
    if (!initialized_) {
        std::cerr << "NixlWrapper::get_metadata_blob: not initialized\n";
        return "";
    }

    try {
        nixl_blob_t metadata;
        auto status = impl_->agent->getLocalMD(metadata);
        if (status != NIXL_SUCCESS) {
            std::cerr << "NixlWrapper::get_metadata_blob: getLocalMD failed with status=" << status << "\n";
            return "";
        }

        return metadata;

    } catch (const std::exception& e) {
        std::cerr << "NixlWrapper::get_metadata_blob: exception: " << e.what() << "\n";
        return "";
    }
}

std::string NixlWrapper::get_metadata_base64() const {
    std::string blob = get_metadata_blob();
    if (blob.empty()) {
        return "";
    }
    std::string encoded = base64_encode(blob);
    std::cerr << "NixlWrapper::get_metadata_base64: raw blob size=" << blob.size()
              << ", base64 size=" << encoded.size() << "\n";
    return encoded;
}

bool NixlWrapper::load_remote_metadata(const std::string& base64_metadata, std::string& agent_name) {
    if (!initialized_) {
        std::cerr << "NixlWrapper::load_remote_metadata: not initialized\n";
        return false;
    }

    if (base64_metadata.empty()) {
        std::cerr << "NixlWrapper::load_remote_metadata: base64_metadata is empty\n";
        return false;
    }

    std::cerr << "NixlWrapper::load_remote_metadata: base64 input size=" << base64_metadata.size() << "\n";

    std::string decoded = base64_decode(base64_metadata);
    if (decoded.empty()) {
        std::cerr << "NixlWrapper::load_remote_metadata: base64_decode failed (input size="
                  << base64_metadata.size() << ")\n";
        return false;
    }

    std::cerr << "NixlWrapper::load_remote_metadata: decoded blob size=" << decoded.size() << "\n";

    return load_remote_metadata_blob(decoded, agent_name);
}

bool NixlWrapper::load_remote_metadata_blob(const std::string& metadata, std::string& agent_name) {
    if (!initialized_) {
        std::cerr << "NixlWrapper::load_remote_metadata_blob: not initialized\n";
        return false;
    }
    if (metadata.empty()) {
        std::cerr << "NixlWrapper::load_remote_metadata_blob: metadata is empty\n";
        return false;
    }

    try {
        auto status = impl_->agent->loadRemoteMD(metadata, agent_name);
        if (status != NIXL_SUCCESS) {
            std::cerr << "NixlWrapper::load_remote_metadata_blob: loadRemoteMD failed with status="
                      << status << " (metadata size=" << metadata.size() << ")\n";
            return false;
        }
        return true;

    } catch (const std::exception& e) {
        std::cerr << "NixlWrapper::load_remote_metadata_blob: exception: " << e.what() << "\n";
        return false;
    }
}

bool NixlWrapper::read_from_remote(const std::string& remote_agent,
                                   void* local_buffer,
                                   uintptr_t remote_addr,
                                   size_t size) {
    if (!initialized_ || !local_buffer || size == 0) {
        return false;
    }

    try {
        // Create local descriptor
        nixl_xfer_dlist_t local_descs(DRAM_SEG);
        nixlBasicDesc local_desc;
        local_desc.addr = reinterpret_cast<uintptr_t>(local_buffer);
        local_desc.len = size;
        local_desc.devId = 0;
        local_descs.addDesc(local_desc);

        // Create remote descriptor with absolute address
        nixl_xfer_dlist_t remote_descs(DRAM_SEG);
        nixlBasicDesc remote_desc;
        remote_desc.addr = remote_addr;
        remote_desc.len = size;
        remote_desc.devId = 0;
        remote_descs.addDesc(remote_desc);

        // Create and execute transfer request
        nixlXferReqH* req = nullptr;
        auto status = impl_->agent->createXferReq(NIXL_READ, local_descs, remote_descs,
                                                   remote_agent, req);
        if (status != NIXL_SUCCESS) {
            return false;
        }

        // Post the request
        status = impl_->agent->postXferReq(req);
        if (status != NIXL_SUCCESS && status != NIXL_IN_PROG) {
            impl_->agent->releaseXferReq(req);
            return false;
        }

        // Wait for completion
        while ((status = impl_->agent->getXferStatus(req)) == NIXL_IN_PROG) {
            // Progress thread handles async completion
        }

        impl_->agent->releaseXferReq(req);
        return status == NIXL_SUCCESS;

    } catch (const std::exception&) {
        return false;
    }
}

bool NixlWrapper::write_to_remote(const std::string& remote_agent,
                                  const void* local_buffer,
                                  uintptr_t remote_addr,
                                  size_t size,
                                  const std::string& notify_msg) {
    if (!initialized_ || !local_buffer || size == 0) {
        return false;
    }

    try {
        // Create local descriptor
        nixl_xfer_dlist_t local_descs(DRAM_SEG);
        nixlBasicDesc local_desc;
        local_desc.addr = reinterpret_cast<uintptr_t>(local_buffer);
        local_desc.len = size;
        local_desc.devId = 0;
        local_descs.addDesc(local_desc);

        // Create remote descriptor with absolute address
        nixl_xfer_dlist_t remote_descs(DRAM_SEG);
        nixlBasicDesc remote_desc;
        remote_desc.addr = remote_addr;
        remote_desc.len = size;
        remote_desc.devId = 0;
        remote_descs.addDesc(remote_desc);

        // Set up optional args for notification
        nixl_opt_args_t opt_args;
        if (!notify_msg.empty()) {
            opt_args.hasNotif = true;
            opt_args.notifMsg = notify_msg;
        }

        // Create and execute transfer request
        nixlXferReqH* req = nullptr;
        auto status = impl_->agent->createXferReq(NIXL_WRITE, local_descs, remote_descs,
                                                   remote_agent, req,
                                                   notify_msg.empty() ? nullptr : &opt_args);
        if (status != NIXL_SUCCESS) {
            return false;
        }

        // Post the request
        status = impl_->agent->postXferReq(req);
        if (status != NIXL_SUCCESS && status != NIXL_IN_PROG) {
            impl_->agent->releaseXferReq(req);
            return false;
        }

        // Wait for completion
        while ((status = impl_->agent->getXferStatus(req)) == NIXL_IN_PROG) {
            // Progress thread handles async completion
        }

        impl_->agent->releaseXferReq(req);
        return status == NIXL_SUCCESS;

    } catch (const std::exception&) {
        return false;
    }
}

std::map<std::string, std::string> NixlWrapper::get_notifications() {
    std::map<std::string, std::string> result;

    if (!initialized_) {
        return result;
    }

    try {
        nixl_notifs_t notif_map;
        auto status = impl_->agent->getNotifs(notif_map);
        if (status == NIXL_SUCCESS) {
            for (const auto& [agent_name, messages] : notif_map) {
                // Combine all messages from this agent (typically just one)
                for (const auto& msg : messages) {
                    result[agent_name] = msg;
                }
            }
        }

    } catch (const std::exception&) {
        // Return empty map on error
    }

    return result;
}

bool NixlWrapper::send_notification(const std::string& agent_name, const std::string& message) {
    if (!initialized_) {
        return false;
    }

    try {
        auto status = impl_->agent->genNotif(agent_name, message);
        return status == NIXL_SUCCESS;

    } catch (const std::exception&) {
        return false;
    }
}

void NixlWrapper::shutdown() {
    if (!initialized_) {
        return;
    }

    if (impl_->registered_buffer != nullptr) {
        deregister_buffer();
    }

    // Agent destructor will clean up backends
    impl_->agent.reset();
    impl_->ucx_backend = nullptr;
    initialized_ = false;
}

} // namespace nixl_topo
