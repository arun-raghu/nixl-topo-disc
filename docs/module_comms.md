---
paths: src/agent/**
---
#Agent/Controller design

## 1. Agent Design

### 1.a Bootstrap

Each agent runs as a container spawned by the Controller via an orchestrator (Docker, Kubernetes). The Controller passes all necessary NIXL metadata via environment variables at container startup, eliminating the need for any TCP handshake.

**Design Principle:** NIXL memory is the ONLY IPC mechanism. No TCP sockets.

**Environment Variables (set by Controller via orchestrator):**

| Variable | Description |
|----------|-------------|
| `CTRL_ENDPOINT` | Base64-encoded controller NIXL endpoint blob |
| `CTRL_BUFFER` | Base64-encoded controller buffer descriptor |
| `AGENT_ID` | Assigned agent ID (0, 1, 2, ...) |
| `NUM_AGENTS` | Total number of agents |

**Startup Sequence:**

1. Agent container starts with env vars already set
2. Parse and decode controller NIXL metadata from env vars
3. Initialize NIXL runtime
4. Allocate and register test buffer (see buffer design below)
5. Serialize own NIXL endpoint metadata + buffer descriptor
6. Connect to controller's NIXL endpoint (using decoded metadata)
7. Read `BufferHeader` from controller buffer to get region offsets
8. Compute all slot offsets (metadata, status, notification, results)
9. Write own endpoint + buffer blobs into metadata slot via NIXL transfer
10. Initialize notifier watching own notification slot
11. Wait for RENDEZVOUS_COMPLETE notification from Controller
12. Read all peer endpoint metadata from Controller buffer
13. Establish NIXL connections to all peer agents
14. Enter event loop (notification-driven, waiting for commands)

### 1.b Inter-Agent Communication via NIXL

#### Agent Buffer Design

**Single buffer per agent, allocated upfront during bootstrap. Size is configurable.**

```cpp
// Buffer sizing defaults (configurable via AgentConfig)
constexpr size_t DEFAULT_WINDOW_SIZE = 64;                    // Outstanding transfers
constexpr size_t DEFAULT_MAX_MESSAGE_SIZE = 4 * 1024 * 1024;  // 4MB
constexpr size_t DEFAULT_BUFFER_SIZE = DEFAULT_WINDOW_SIZE * DEFAULT_MAX_MESSAGE_SIZE;  // 256MB

struct AgentConfig {
    // Buffer configuration
    size_t max_message_size = DEFAULT_MAX_MESSAGE_SIZE;  // 4MB default
    size_t window_size = DEFAULT_WINDOW_SIZE;            // 64 default

    // Computed buffer size
    size_t buffer_size() const { return max_message_size * window_size; }

    // Examples:
    //   Default:      4MB × 64 = 256MB
    //   High-BW:      8MB × 64 = 512MB  (for NVLink saturation)
    //   Memory-tight: 4MB × 32 = 128MB
};
```

**Buffer Layout:**
```
Agent Test Buffer (configurable, default 256MB):
┌─────────────────────────────────────────────────────────────────────┐
│ TRANSFER SLOTS (for windowed bandwidth test)                        │
│ ┌────────┬────────┬────────┬─────────────────────┬────────────────┐ │
│ │ slot 0 │ slot 1 │ slot 2 │ ...                 │ slot [W-1]     │ │
│ │ (M MB) │ (M MB) │ (M MB) │                     │ (M MB)         │ │
│ └────────┴────────┴────────┴─────────────────────┴────────────────┘ │
│   W = window_size (default 64)                                      │
│   M = max_message_size (default 4MB)                                │
│   Total = W × M (default 256MB)                                     │
│                                                                     │
│ Usage by test type:                                                 │
│ - PAIRWISE_LATENCY: slot 0 only (8 bytes used)                     │
│ - BANDWIDTH: all W slots (full window pipelining)                   │
│ - BIDIRECTIONAL: all W slots                                        │
│ - CONCURRENT_BANDWIDTH: all W slots                                 │
└─────────────────────────────────────────────────────────────────────┘
```

**Rationale for upfront allocation:**
- Single metadata share during rendezvous (no per-test synchronization)
- 256MB enables full 64-message window for accurate peak bandwidth measurement
- Same buffer reused for all test types
- Simpler implementation, avoids dynamic buffer management

#### Agent Structure

```cpp
class Agent {
    // Configuration
    AgentConfig config_;

    // Identity
    uint32_t my_id_;

    // NIXL handles
    NixlEndpoint endpoint_;

    // Test buffer (configurable size, registered with NIXL)
    void* test_buffer_;
    size_t test_buffer_size_;
    NixlBufferDescriptor test_buffer_desc_;

    // Controller connection
    NixlBufferDescriptor controller_buffer_desc_;
    NixlEndpoint controller_endpoint_;

    // Peer connections (populated after rendezvous)
    struct PeerInfo {
        uint32_t peer_id;
        NixlEndpoint endpoint;
        NixlBufferDescriptor buffer_desc;
    };
    vector<PeerInfo> peers_;

    // Computed offsets in controller buffer
    size_t metadata_offset_;
    size_t status_offset_;
    size_t notification_offset_;
    size_t results_offset_;

public:
    void initialize(const AgentConfig& config) {
        config_ = config;

        // 1. Parse controller metadata from environment variables
        parse_env_vars();

        // 2. Init NIXL
        endpoint_ = nixl_create_endpoint();

        // 3. Allocate and register test buffer (size from config)
        test_buffer_size_ = config_.buffer_size();
        test_buffer_ = allocate_registered_memory(test_buffer_size_);
        test_buffer_desc_ = nixl_register_buffer(endpoint_, test_buffer_, test_buffer_size_);

        LOG_INFO("Agent {} buffer allocated: {} MB ({}x {} byte slots)",
                 my_id_,
                 test_buffer_size_ / (1024*1024),
                 config_.window_size,
                 config_.max_message_size);

        // 4. Connect to controller endpoint (using metadata from env vars)
        connect_to_controller();

        // 5. Read header and compute all slot offsets
        read_header_and_compute_offsets();

        // 6. Write our metadata to controller buffer via NIXL
        write_metadata_to_controller();

        // 7. Initialize notifications and wait for rendezvous completion
        initialize_notifications();
        wait_for_rendezvous_notification();

        // 8. Read all peer metadata
        read_peer_metadata();

        // 9. Enter event loop (notification-driven)
        event_loop();
    }

private:
    void parse_env_vars() {
        // Parse agent ID and count
        my_id_ = std::stoul(std::getenv("AGENT_ID"));
        num_agents_ = std::stoul(std::getenv("NUM_AGENTS"));

        // Decode controller NIXL metadata from base64
        std::string ctrl_endpoint_b64 = std::getenv("CTRL_ENDPOINT");
        std::string ctrl_buffer_b64 = std::getenv("CTRL_BUFFER");

        controller_endpoint_ = deserialize_endpoint(base64_decode(ctrl_endpoint_b64));
        controller_buffer_desc_ = deserialize_buffer(base64_decode(ctrl_buffer_b64));

        LOG_INFO("Agent {} initialized from env vars, controller metadata decoded", my_id_);
    }

    void connect_to_controller() {
        // Establish NIXL connection to controller using pre-parsed metadata
        nixl_connect(endpoint_, controller_endpoint_);
        LOG_INFO("Agent {} connected to controller via NIXL", my_id_);
    }

    void read_header_and_compute_offsets() {
        // Read BufferHeader from controller buffer to get region offsets
        BufferHeader header;
        nixl_read(controller_endpoint_, controller_buffer_desc_,
                  0, &header, sizeof(header));

        // Compute this agent's specific slot offsets
        metadata_offset_ = header.agent_slots_offset + (my_id_ * AGENT_SLOT_SIZE);
        status_offset_ = header.test_ctrl_offset + sizeof(TestCommand) + (my_id_ * AGENT_STATUS_SIZE);
        notification_offset_ = header.notification_offset + (my_id_ * NOTIFICATION_SLOT_SIZE);
        results_offset_ = header.results_offset + (my_id_ * AGENT_RESULT_SIZE);

        LOG_INFO("Agent {} offsets computed: metadata={}, status={}, notification={}, results={}",
                 my_id_, metadata_offset_, status_offset_, notification_offset_, results_offset_);
    }

    void wait_for_rendezvous_notification() {
        // Block until RENDEZVOUS_COMPLETE notification received
        while (true) {
            bool notified = notifier_.wait(RENDEZVOUS_TIMEOUT_MS);
            if (notified) {
                Notification n = read_notification();
                if (n.type == NotificationType::RENDEZVOUS_COMPLETE) {
                    LOG_INFO("Agent {} received rendezvous complete notification", my_id_);
                    return;
                }
            }
        }
    }
};
```

#### Buffer Access Helpers

```cpp
// Get pointer to specific slot in test buffer
void* Agent::get_slot_ptr(uint32_t slot_index) {
    assert(slot_index < config_.window_size);
    return static_cast<uint8_t*>(test_buffer_) + (slot_index * config_.max_message_size);
}

// Get offset for NIXL transfer
size_t Agent::get_slot_offset(uint32_t slot_index) {
    return slot_index * config_.max_message_size;
}

// For latency test: use first 8 bytes of slot 0
void* Agent::get_latency_buffer() {
    return test_buffer_;
}

// Validate test config against buffer capacity
bool Agent::can_run_test(const TestConfig& test_cfg) {
    for (auto msg_size : test_cfg.message_sizes) {
        if (msg_size > config_.max_message_size) {
            LOG_ERROR("Test requires {} byte messages, but buffer slots are {} bytes",
                      msg_size, config_.max_message_size);
            return false;
        }
    }
    return true;
}
```

#### Operations

**Basic Operations:**
- `nixl_write(peer_id, local_offset, remote_offset, size)` - One-sided write to peer memory
- `nixl_read(peer_id, remote_offset, local_offset, size)` - One-sided read from peer memory
- `nixl_write_async(peer_id, local_offset, remote_offset, size)` - Async write, returns handle
- `nixl_wait(handle)` - Wait for async operation completion

#### Notification Mechanism

**Design replaces busy-polling with NIXL/UCX notification callbacks for efficient async coordination.**

##### Notification Architecture

```cpp
// Notification types for different events
enum class NotificationType : uint32_t {
    COMMAND_READY,        // New command available from controller
    AGENT_STATUS_CHANGED, // Agent status updated
    TRANSFER_COMPLETE,    // NIXL transfer finished
    RENDEZVOUS_COMPLETE,  // All agents registered
    RESULTS_READY         // Results available for collection
};

// Notification payload written to notification slot
struct Notification {
    uint64_t sequence;            // Monotonically increasing
    NotificationType type;
    uint32_t source_id;           // Agent or controller ID
    uint64_t timestamp_ns;
    uint64_t payload;             // Type-specific data (e.g., command_seq)
};

// Each agent has a notification slot in controller buffer
constexpr size_t NOTIFICATION_SLOT_SIZE = 64;
```

##### Controller Buffer Extension

```
Controller Buffer Layout (updated):
┌─────────────────────────────────────────────────────────────────────┐
│ HEADER                                                              │
├─────────────────────────────────────────────────────────────────────┤
│ AGENT METADATA SLOTS                                                │
├─────────────────────────────────────────────────────────────────────┤
│ TEST CONTROL REGION                                                 │
├─────────────────────────────────────────────────────────────────────┤
│ NOTIFICATION SLOTS (NEW)                                            │
│   [Agent 0 notify] [Agent 1 notify] ... [Agent N-1 notify]         │
│   Controller writes here to wake agents                             │
├─────────────────────────────────────────────────────────────────────┤
│ RESULTS REGION                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

##### NIXL Notification API

```cpp
// Notification callback signature
using NixlNotifyCallback = std::function<void(
    const Notification& notification,
    void* user_context
)>;

// Register callback for memory region changes
class NixlNotifier {
public:
    // Create notifier watching a memory region
    NixlNotifier(NixlEndpoint endpoint, void* watch_addr, size_t watch_size);

    // Register callback - called when watched region is written
    void set_callback(NixlNotifyCallback callback, void* context);

    // Arm notification - must be called after each callback
    void arm();

    // Get file descriptor for epoll/select integration
    int get_event_fd();

    // Block until notification (with timeout)
    // Returns: true if notified, false if timeout
    bool wait(uint64_t timeout_ms = UINT64_MAX);
};

// UCX-based implementation using ucp_worker_arm / ucp_worker_signal
class UcxNotifier : public NixlNotifier {
    ucp_worker_h worker_;
    int event_fd_;

public:
    UcxNotifier(ucp_worker_h worker) : worker_(worker) {
        // Get event FD for epoll integration
        ucp_worker_get_efd(worker_, &event_fd_);
    }

    void arm() {
        // Arm worker for next event
        ucs_status_t status = ucp_worker_arm(worker_);
        if (status == UCS_ERR_BUSY) {
            // Events already pending, callback will fire
        }
    }

    bool wait(uint64_t timeout_ms) {
        struct pollfd pfd = {event_fd_, POLLIN, 0};
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret > 0) {
            ucp_worker_progress(worker_);
            return true;
        }
        return false;
    }
};
```

##### Agent Event Loop (Notification-Based)

```cpp
class Agent {
    NixlNotifier notifier_;
    std::atomic<uint64_t> last_seen_seq_{0};

public:
    void initialize_notifications() {
        // Watch our notification slot in controller buffer
        void* my_notify_slot = get_my_notification_slot();
        notifier_ = NixlNotifier(endpoint_, my_notify_slot, NOTIFICATION_SLOT_SIZE);

        // Set callback for incoming notifications
        notifier_.set_callback(
            [this](const Notification& n, void* ctx) {
                handle_notification(n);
            },
            this
        );
        notifier_.arm();
    }

    void event_loop() {
        while (running_) {
            // Block until notification or timeout (for heartbeat)
            bool notified = notifier_.wait(HEARTBEAT_INTERVAL_MS);

            if (notified) {
                // Process any pending work
                process_pending_commands();
            }

            // Periodic heartbeat regardless of notifications
            update_heartbeat();
        }
    }

    void handle_notification(const Notification& n) {
        switch (n.type) {
            case NotificationType::COMMAND_READY:
                // New command available, will be processed in event_loop
                LOG_DEBUG("Command notification received, seq={}", n.payload);
                break;

            case NotificationType::RENDEZVOUS_COMPLETE:
                // Can now read peer metadata
                read_peer_metadata();
                break;

            default:
                LOG_WARN("Unknown notification type: {}", n.type);
        }
        notifier_.arm();  // Re-arm for next notification
    }

    void process_pending_commands() {
        // Read current command
        TestCommand cmd = nixl_read_test_command(controller_buffer_);

        if (cmd.command_seq <= last_seen_seq_) {
            return;  // Already processed
        }
        last_seen_seq_ = cmd.command_seq;

        // Handle command (same as before)
        switch (cmd.command_type) {
            case CommandType::CONFIGURE:
                handle_configure(cmd.test_config);
                update_status(AgentState::READY);
                break;
            // ... other commands ...
        }
    }
};
```

##### Controller Notification Dispatch

```cpp
class Controller {
    vector<NixlNotifier> agent_notifiers_;  // Watch agent status slots

public:
    void initialize_notifications() {
        // Create notifier for each agent's status slot
        for (uint32_t i = 0; i < num_agents_; i++) {
            void* status_slot = get_agent_status_slot(i);
            agent_notifiers_.emplace_back(endpoint_, status_slot, AGENT_STATUS_SIZE);
        }
    }

    // Send notification to specific agent
    void notify_agent(uint32_t agent_id, NotificationType type, uint64_t payload = 0) {
        Notification n;
        n.sequence = next_notify_seq_++;
        n.type = type;
        n.source_id = CONTROLLER_ID;
        n.timestamp_ns = now_ns();
        n.payload = payload;

        // Write to agent's notification slot
        void* slot = get_agent_notification_slot(agent_id);
        nixl_write(agents_[agent_id].endpoint,
                   &n, sizeof(n),
                   slot, sizeof(n));

        LOG_DEBUG("Notified agent {} type={} seq={}", agent_id, type, n.sequence);
    }

    // Send notification to all agents
    void notify_all_agents(NotificationType type, uint64_t payload = 0) {
        for (uint32_t i = 0; i < num_agents_; i++) {
            notify_agent(i, type, payload);
        }
    }

    // Wait for agents using notifications instead of polling
    WaitResult wait_for_agents_state(
        const vector<uint32_t>& agent_ids,
        AgentState expected,
        uint64_t timeout_ms
    ) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        std::set<uint32_t> pending(agent_ids.begin(), agent_ids.end());

        while (!pending.empty()) {
            // Calculate remaining timeout
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return WaitResult::TIMEOUT;
            }
            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count();

            // Use epoll to wait on all pending agent notifiers
            bool any_notified = wait_any_agent_notification(pending, remaining_ms);

            if (any_notified) {
                // Check which agents reached expected state
                for (auto it = pending.begin(); it != pending.end(); ) {
                    uint32_t id = *it;
                    auto* status = get_agent_status(id);

                    if (status->state == expected) {
                        it = pending.erase(it);
                    } else if (check_agent_health(id) == AgentHealth::DEAD) {
                        return WaitResult::AGENT_DEAD;
                    } else {
                        ++it;
                    }
                }
            }
        }
        return WaitResult::SUCCESS;
    }

private:
    bool wait_any_agent_notification(const std::set<uint32_t>& agent_ids, uint64_t timeout_ms) {
        // Build epoll set for pending agents
        int epfd = epoll_create1(0);
        for (uint32_t id : agent_ids) {
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.u32 = id;
            epoll_ctl(epfd, EPOLL_CTL_ADD, agent_notifiers_[id].get_event_fd(), &ev);
        }

        // Wait for any notification
        struct epoll_event events[16];
        int nfds = epoll_wait(epfd, events, 16, timeout_ms);
        close(epfd);

        if (nfds > 0) {
            // Progress UCX workers for notified agents
            for (int i = 0; i < nfds; i++) {
                uint32_t agent_id = events[i].data.u32;
                agent_notifiers_[agent_id].arm();
            }
            return true;
        }
        return false;
    }
};
```

##### Test Orchestration with Notifications

```cpp
void TestManager::run_pairwise_test(TestType type) {
    auto* cmd = buffer_->get_test_command();

    // 1. Configure all agents
    cmd->command_seq++;
    cmd->command_type = CommandType::CONFIGURE;
    cmd->test_config.type = type;

    // Notify all agents of new command
    notify_all_agents(NotificationType::COMMAND_READY, cmd->command_seq);

    // Wait for READY state (notification-based, not polling)
    auto result = wait_for_agents_state(all_agent_ids(), AgentState::READY,
                                         config_.configure_timeout_ms);
    if (result != WaitResult::SUCCESS) {
        handle_wait_failure(result);
        return;
    }

    // 2. Execute pairs
    for (const auto& pair : pairs) {
        cmd->command_seq++;
        cmd->num_active_pairs = 1;
        cmd->active_pairs[0] = {pair.first, pair.second};
        cmd->command_type = CommandType::START;

        // Notify only the active pair
        notify_agent(pair.first, NotificationType::COMMAND_READY, cmd->command_seq);
        notify_agent(pair.second, NotificationType::COMMAND_READY, cmd->command_seq);

        // Wait for pair to complete
        result = wait_for_agents_state({pair.first, pair.second}, AgentState::DONE,
                                        config_.test_pair_timeout_ms);
        // ... handle result ...
    }

    // 3. Collect results
    cmd->command_seq++;
    cmd->command_type = CommandType::COLLECT;
    notify_all_agents(NotificationType::COMMAND_READY, cmd->command_seq);
    // ... wait and collect ...
}
```

##### Benefits of Notification-Based Design

| Aspect | Polling | Notifications |
|--------|---------|---------------|
| **CPU usage** | High (continuous polling) | Low (sleep between events) |
| **Latency** | Depends on poll interval | Immediate wakeup |
| **Scalability** | O(N) CPU per poll cycle | O(1) per event |
| **Power** | High (no sleep) | Low (kernel sleep) |
| **Complexity** | Simple | Moderate (callback management) |

##### Fallback to Polling

For environments where UCX notifications are unavailable:

```cpp
class PollingNotifier : public NixlNotifier {
    void* watch_addr_;
    uint64_t last_value_;

public:
    bool wait(uint64_t timeout_ms) override {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            uint64_t current = *static_cast<volatile uint64_t*>(watch_addr_);
            if (current != last_value_) {
                last_value_ = current;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return false;
    }
};

// Factory function selects implementation
std::unique_ptr<NixlNotifier> create_notifier(NixlEndpoint ep, void* addr, size_t size) {
    if (ucx_notifications_available()) {
        return std::make_unique<UcxNotifier>(ep, addr, size);
    } else {
        LOG_WARN("UCX notifications unavailable, falling back to polling");
        return std::make_unique<PollingNotifier>(addr, size);
    }
}
```

### 1.c Memory Performance Test Suite

The test suite is organized into phases that support **incremental topology discovery**:

| Test Phase | Test | Enables Graph Level | Run Time |
|------------|------|---------------------|----------|
| **Phase 1** | `PAIRWISE_LATENCY` | Level 1: Quick topology | Minutes |
| **Phase 2** | `BANDWIDTH`, `BIDIRECTIONAL`, `LATENCY_SWEEP` | Level 2: Measured capacities | 10-30 min |
| **Phase 2b** | `CONCURRENT_BANDWIDTH` | Level 3: Bottleneck detection | Hours |

**Incremental Approach:** Run Phase 1 first for rapid topology overview. Add Phase 2/2b as needed for richer analysis. See Section 3.a for graph construction details.

---

#### Implementation Requirements

**Thread Pinning (Mandatory):**
```cpp
// Pin test thread to specific CPU core to reduce jitter
void pin_thread(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        LOG_WARN("Thread pinning failed for CPU {}: {}", cpu_id, strerror(rc));
    }
}
```
- Agent config specifies `cpu_affinity` parameter
- Log warning if pinning fails but continue execution

**Zero-Copy Transfers:**
- All NIXL transfers MUST use registered memory (zero-copy by default)
- If a transfer requires staging through non-registered memory:
  ```cpp
  LOG_WARN("[NON-ZERO-COPY] Transfer {}->{} using staged copy, size={}",
           src_id, dst_id, size);
  ```
- Track and report non-zero-copy transfer count in test results

---

#### Phase 1: Topology Detection (Implement First)

**Goal:** Produce NxN latency matrix sufficient for topology inference via clustering.

##### Test: `PAIRWISE_LATENCY`

Ping-pong latency measurement between all agent pairs. Reference: OSU `osu_latency`.

**Protocol:**
```
Agent A (sender)                    Agent B (responder)
    |                                      |
    |-- NIXL_WRITE (8 bytes, flag=1) ----->|
    |                                      | (polls for flag)
    |<-- NIXL_WRITE (8 bytes, flag=1) -----|
    |  (polls for flag)                    |
    |                                      |
    t_rtt = t_end - t_start
    latency = t_rtt / 2
```

**Parameters (OMB-aligned):**
| Parameter | Value | Notes |
|-----------|-------|-------|
| Message size | 8 bytes | Minimum size, measures true latency |
| Warmup iterations | 1000 | Discard, not recorded |
| Measured iterations | 10000 | Used for statistics |
| Sync | Barrier before each pair | Prevents interference |

**Test Execution (Orchestrated by Controller):**

```cpp
struct PairwiseLatencyConfig {
    uint32_t message_size = 8;
    uint32_t warmup_iters = 1000;
    uint32_t measure_iters = 10000;
};

// Scheduling: round-robin pairs to minimize contention
// For N agents, test N*(N-1)/2 unique pairs
// Pairs tested sequentially to avoid network interference
```

**Pair Scheduling:**
1. Controller generates pair schedule: `[(0,1), (0,2), ..., (N-2, N-1)]`
2. For each pair `(A, B)`:
   - Controller signals A and B to begin
   - Other agents idle (barrier wait)
   - A and B execute ping-pong
   - Results recorded locally
3. After all pairs complete, agents upload results

**Output: Latency Matrix**
```cpp
struct LatencyMatrix {
    uint32_t num_agents;
    // latency[i][j] = one-way latency from agent i to agent j (nanoseconds)
    // latency[i][i] = 0 (self)
    // Matrix is approximately symmetric; store both directions
    vector<vector<LatencyStats>> latency;
};

struct LatencyStats {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t avg_ns;
    uint64_t p50_ns;
    uint64_t p99_ns;
    uint64_t stddev_ns;
};
```

**Topology Inference:** See Section 3.a Graph Construction for the complete algorithm. The latency matrix produced here is the primary input.

---

#### Phase 2: Performance Characterization (Implement Second)

**Goal:** Full characterization of memory transfer performance. Reference: OSU micro-benchmarks.

##### Test: `LATENCY_SWEEP`

Latency vs message size curve. Reference: OSU `osu_latency` with size sweep.

**Parameters:**
| Parameter | Value |
|-----------|-------|
| Message sizes | 8, 16, 32, 64, 128, 256, 512, 1K, 2K, 4K, 8K, 16K, 32K, 64K |
| Warmup | 100 iterations per size |
| Measured | 1000 iterations per size |

**Output:** Latency vs size curve per pair, reveals:
- Small message latency (network latency dominated)
- Crossover point to bandwidth-dominated regime

##### Test: `BANDWIDTH`

Unidirectional streaming bandwidth. Reference: OSU `osu_bw`.

**Protocol:**
```
Agent A (sender)                    Agent B (receiver)
    |                                      |
    |-- NIXL_WRITE (window of N msgs) ---->|
    |-- NIXL_WRITE ----------------------->|
    |-- ... (window_size msgs) ----------->|
    |                                      |
    |<-- ACK (single completion) ----------|
    |                                      |
    bandwidth = (window_size * msg_size) / time
```

**Parameters (OMB-aligned):**
| Parameter | Value | Notes |
|-----------|-------|-------|
| Message sizes | 1K, 4K, 16K, 64K, 256K, 1M, 4M | Large messages |
| Window size | 64 | Outstanding operations |
| Warmup | 10 iterations |
| Measured | 100 iterations |

**Output:**
```cpp
struct BandwidthStats {
    uint64_t message_size;
    double bandwidth_gbps;      // GB/s
    double bandwidth_msgs_sec;  // Messages/second
};

// Aggregated bandwidth matrix for graph construction (Phase 3)
struct BandwidthMatrix {
    uint32_t num_agents;
    uint64_t reference_msg_size;  // Message size used (e.g., 4MB for peak BW)
    // bandwidth[i][j] = measured bandwidth from agent i to agent j
    vector<vector<double>> bandwidth_gbps;
};
```

##### Test: `BIDIRECTIONAL`

Simultaneous send and receive. Reference: OSU `osu_bibw`.

**Protocol:**
- Both agents simultaneously send window of messages to each other
- Measures full-duplex capability of the link

**Parameters:** Same as `BANDWIDTH`

**Output:**
```cpp
struct BidirectionalStats {
    uint64_t message_size;
    double aggregate_bandwidth_gbps;  // Sum of both directions
    double a_to_b_gbps;
    double b_to_a_gbps;
};
```

##### Test: `CONCURRENT_BANDWIDTH` (For Topology Phase 4)

Detect shared bottlenecks by measuring bandwidth degradation under concurrent load.

**Goal:** Produce data for graph construction Phase 4 (shared bottleneck detection).

**Protocol:**
```
Phase A: Solo measurements (baseline)
    For each pair (i,j):
        Run BANDWIDTH test in isolation
        Record: solo_bandwidth[i][j]

Phase B: Concurrent measurements
    For each pair combination ((i,j), (k,l)) where i,j,k,l distinct:
        Run both pairs simultaneously
        Record: concurrent_bandwidth[i][j], concurrent_bandwidth[k][l]
```

**Parameters:**
| Parameter | Value | Notes |
|-----------|-------|-------|
| Message size | 4MB | Large message for bandwidth saturation |
| Window size | 64 | |
| Iterations | 50 | Per pair combination |
| Pair sampling | Strategic | Not all O(N^4) combinations; sample across tiers |

**Pair Selection Strategy:**
```cpp
// Don't test all combinations - combinatorial explosion
// Instead, sample strategically:
vector<PairCombination> select_concurrent_pairs(
    const vector<uint32_t>& tier_assignments,
    uint32_t samples_per_tier_combo
) {
    vector<PairCombination> combos;

    // 1. Intra-tier pairs: likely share switches
    //    Sample pairs within same tier cluster
    // 2. Cross-tier pairs: test spine/aggregation bottlenecks
    //    Sample pairs from different tier clusters
    // 3. Adjacent pairs: test for NVSwitch contention
    //    Pairs sharing a node

    return combos;  // Limited to ~O(N^2) combinations
}
```

**Output:**
```cpp
struct ConcurrentTestResult {
    pair<uint32_t, uint32_t> pair1;
    pair<uint32_t, uint32_t> pair2;
    double pair1_solo_bw;       // Bandwidth when pair1 runs alone
    double pair2_solo_bw;       // Bandwidth when pair2 runs alone
    double pair1_concurrent_bw; // Bandwidth when both run together
    double pair2_concurrent_bw;
    double degradation_ratio_1; // (solo - concurrent) / solo
    double degradation_ratio_2;
};

struct ConcurrentTestResults {
    vector<ConcurrentTestResult> results;
    uint64_t message_size;
    uint32_t iterations;
};
```

**Note:** This test is optional and only needed if Phase 4 bottleneck detection is enabled. It significantly increases test time due to pair combinations.

---

#### Test Configuration Structure

```cpp
enum class TestType {
    // Phase 1: Topology Detection
    PAIRWISE_LATENCY,

    // Phase 2: Performance Characterization
    LATENCY_SWEEP,
    BANDWIDTH,
    BIDIRECTIONAL,

    // Phase 2b: Bottleneck Detection (optional, for graph Phase 4)
    CONCURRENT_BANDWIDTH
};

struct TestConfig {
    TestType type;

    // Common
    uint32_t warmup_iterations;
    uint32_t measure_iterations;
    int cpu_affinity;               // -1 = no pinning

    // Size parameters
    vector<uint64_t> message_sizes; // Empty = use defaults for test type

    // Bandwidth-specific
    uint32_t window_size = 64;

    // Pair selection
    enum PairMode { ALL_PAIRS, SPECIFIC_PAIRS };
    PairMode pair_mode = ALL_PAIRS;
    vector<pair<uint32_t, uint32_t>> specific_pairs;  // If SPECIFIC_PAIRS
};
```

---

#### Test Execution Flow

**All communication via NIXL memory.** See Section 2.b for detailed protocol.

```
┌─────────────────────────────────────────────────────────────┐
│ CONTROLLER (writes to test control buffer)                  │
├─────────────────────────────────────────────────────────────┤
│  1. Write CONFIGURE command + TestConfig                    │
│  2. Poll agent status slots for READY                       │
│  3. For each pair/batch in schedule:                        │
│     a. Write START command + active_pairs[(A,B)]            │
│     b. Poll active agent status for DONE                    │
│  4. Write COLLECT command                                   │
│  5. Read results from results region                        │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ AGENT (polls test control buffer, writes status/results)    │
├─────────────────────────────────────────────────────────────┤
│  1. Poll for new command_seq                                │
│  2. On CONFIGURE: pin thread, alloc buffers, write READY    │
│  3. On START: check if in active_pairs                      │
│     - If active: write RUNNING, execute, write DONE         │
│     - If idle: no action                                    │
│  4. On COLLECT: write results to results region             │
└─────────────────────────────────────────────────────────────┘
```

---

#### Measurement & Timing

```cpp
// High-resolution timing
inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// Or use std::chrono::steady_clock for portability
inline uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

// Calibrate TSC frequency at startup for rdtsc users
```

**Statistical Aggregation:**
```cpp
struct Measurements {
    vector<uint64_t> samples;

    LatencyStats compute_stats() {
        sort(samples.begin(), samples.end());
        size_t n = samples.size();
        return {
            .min_ns = samples[0],
            .max_ns = samples[n-1],
            .avg_ns = accumulate(samples.begin(), samples.end(), 0UL) / n,
            .p50_ns = samples[n / 2],
            .p99_ns = samples[n * 99 / 100],
            .stddev_ns = compute_stddev(samples)
        };
    }
};
```

### 1.d Persisting Performance Data

Test results are persisted in two tiers:
1. **Minimal results** → Uploaded to Controller for graph construction
2. **Full statistics** → Stored in agent memory, fetched via NIXL on demand

---

#### Tier 1: Minimal Results (Uploaded to Controller)

Only initiator agents upload results. Upload occurs once per test type after all pairs complete.

```cpp
// Minimal structures - only values needed for graph construction

struct LatencyResult {
    uint32_t src_agent_id;
    uint32_t dst_agent_id;
    uint64_t message_size;
    uint64_t avg_latency_ns;      // Graph uses only avg
};

struct BandwidthResult {
    uint32_t src_agent_id;
    uint32_t dst_agent_id;
    uint64_t message_size;
    double bandwidth_gbps;        // Graph uses peak BW
};

struct ConcurrentBandwidthResult {
    uint32_t src_agent_id;
    uint32_t dst_agent_id;
    uint32_t concurrent_src_id;   // The other pair
    uint32_t concurrent_dst_id;
    double solo_bandwidth_gbps;
    double concurrent_bandwidth_gbps;
};

// Written to agent's result slot in Controller buffer
struct AgentResultsUpload {
    uint32_t agent_id;
    uint32_t num_latency_results;
    uint32_t num_bandwidth_results;
    uint32_t num_concurrent_results;
    // Followed by arrays of results
};
```

---

#### Tier 2: Full Statistics (Agent Memory, NIXL Fetch)

Agents retain complete statistics in memory for detailed analysis.

```cpp
// Extended structures with full statistics

struct LatencyResultFull {
    uint32_t src_agent_id;
    uint32_t dst_agent_id;
    uint64_t message_size;
    uint32_t iterations;
    uint64_t timestamp;
    // Full statistics
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t avg_latency_ns;
    uint64_t p50_latency_ns;
    uint64_t p99_latency_ns;
    uint64_t stddev_ns;
};

struct BandwidthResultFull {
    uint32_t src_agent_id;
    uint32_t dst_agent_id;
    uint64_t message_size;
    uint32_t iterations;
    uint64_t timestamp;
    double min_bandwidth_gbps;
    double max_bandwidth_gbps;
    double avg_bandwidth_gbps;
    double stddev_gbps;
};
```

**Agent stores full stats in a dedicated buffer region:**

```
Agent Test Buffer (256MB default):
┌─────────────────────────────────────────────────────────────────────┐
│ TRANSFER SLOTS (0 to 252MB)                                         │
│   [slot 0] [slot 1] ... [slot 63]  (for test data transfers)       │
├─────────────────────────────────────────────────────────────────────┤
│ FULL STATS REGION (last 4MB)                                        │
│   Serialized LatencyResultFull[], BandwidthResultFull[]            │
│   Controller can read this region via NIXL on demand                │
└─────────────────────────────────────────────────────────────────────┘
```

---

#### Fetching Full Statistics via NIXL

> **Implementation Note:** Full statistics fetching is a **Phase 2 feature**. Implement Tier 1 (minimal results upload) first. Add FETCH_FULL_STATS support in a later phase when debugging/analysis capabilities are needed.

Controller retrieves full stats without HTTP/SSH - works in containers.

**Protocol:**

```
Controller                              Agent
    |                                      |
    | 1. Write FETCH_STATS command         |
    |    to test control buffer            |
    |         │                            |
    |         └──── NIXL ─────────────────>|
    |                                      |
    |                            2. Agent serializes full stats
    |                               to FULL_STATS_REGION of its buffer
    |                               Writes status = STATS_READY
    |         ┌──── NIXL ◀─────────────────|
    |         │                            |
    | 3. Controller reads agent's          |
    |    FULL_STATS_REGION via NIXL        |
    |                                      |
```

**Command addition:**

```cpp
enum class CommandType : uint32_t {
    IDLE = 0,
    CONFIGURE,
    START,
    STOP,
    COLLECT,          // Upload minimal results
    FETCH_FULL_STATS  // Prepare full stats for NIXL read
};

enum class AgentState : uint32_t {
    IDLE = 0,
    READY,
    RUNNING,
    DONE,
    ERROR,
    STATS_READY       // Full stats prepared in buffer
};
```

**Controller fetch implementation:**

```cpp
map<uint32_t, vector<LatencyResultFull>> Controller::fetch_full_stats() {
    // 1. Send FETCH_FULL_STATS command
    auto* cmd = buffer_->get_test_command();
    cmd->command_seq++;
    cmd->command_type = CommandType::FETCH_FULL_STATS;
    cmd->num_active_pairs = 0;  // All agents respond

    // 2. Wait for all agents to report STATS_READY
    wait_for_all_agents_state(AgentState::STATS_READY);

    // 3. Read full stats from each agent's buffer via NIXL
    map<uint32_t, vector<LatencyResultFull>> all_stats;
    for (uint32_t agent = 0; agent < num_agents_; agent++) {
        auto& peer = peers_[agent];
        size_t stats_offset = AGENT_BUFFER_SIZE - FULL_STATS_REGION_SIZE;

        // NIXL read from agent's full stats region
        void* local_buf = allocate_temp_buffer(FULL_STATS_REGION_SIZE);
        nixl_read(peer.endpoint, peer.buffer_desc,
                  stats_offset, local_buf, FULL_STATS_REGION_SIZE);

        all_stats[agent] = deserialize_full_stats(local_buf);
    }
    return all_stats;
}
```

---

#### Persistence Strategy Summary

| Data | Storage | Access Method | Use Case |
|------|---------|---------------|----------|
| Minimal results | Controller buffer | Automatic upload | Graph construction |
| Full statistics | Agent buffer | NIXL fetch on demand | Debugging, analysis |

**Upload timing:**
- Minimal: After all pairs complete for a test type
- Full stats: On-demand via `FETCH_FULL_STATS` command

---

## 2. Controller Design

### 2.a Startup and Agent Rendezvous

The Controller orchestrates agent spawning via a container orchestrator (Docker, Kubernetes) and manages rendezvous.

**Design Principle:** NIXL memory is the ONLY IPC mechanism. Controller uses orchestrator API to spawn agent containers with NIXL metadata passed via environment variables.

---

#### Controller Buffer Layout

Single unified buffer for rendezvous and test control:

```
Controller Buffer Layout:
┌─────────────────────────────────────────────────────────────────────┐
│ HEADER (offset 0)                                                   │
│   uint32_t num_agents;                                              │
│   uint32_t agent_slots_offset;      // Offset to agent metadata     │
│   uint32_t test_ctrl_offset;        // Offset to test control region│
│   uint32_t results_offset;          // Offset to results region     │
│   uint64_t ready_flag;              // Set when rendezvous complete │
├─────────────────────────────────────────────────────────────────────┤
│ AGENT METADATA SLOTS (offset: agent_slots_offset)                   │
│   [Slot 0] [Slot 1] ... [Slot N-1]                                  │
│   Each slot (AGENT_SLOT_SIZE bytes):                                │
│     - uint64_t populated_flag;      // Non-zero when written        │
│     - uint8_t  nixl_endpoint_blob[ENDPOINT_BLOB_SIZE];              │
│     - uint8_t  buffer_blob[BUFFER_BLOB_SIZE];                       │
├─────────────────────────────────────────────────────────────────────┤
│ TEST CONTROL REGION (offset: test_ctrl_offset)                      │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │ TestCommand (controller writes, agents poll & read)         │   │
│   │   uint64_t command_seq;         // Incremented per command  │   │
│   │   CommandType command_type;     // IDLE/CONFIGURE/START/STOP│   │
│   │   TestConfig test_config;       // Test parameters          │   │
│   │   uint32_t num_active_pairs;                                │   │
│   │   ActivePair active_pairs[MAX_CONCURRENT_PAIRS];            │   │
│   └─────────────────────────────────────────────────────────────┘   │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │ Agent Status Slots (agents write, controller polls)         │   │
│   │   [Status 0] [Status 1] ... [Status N-1]                    │   │
│   │   Each slot (AGENT_STATUS_SIZE bytes):                      │   │
│   │     - uint64_t status_seq;      // Incremented on update    │   │
│   │     - AgentState state;         // IDLE/READY/RUNNING/DONE  │   │
│   │     - uint64_t last_ack_seq;    // Last command_seq ack'd   │   │
│   │     - uint32_t error_code;      // 0 = no error             │   │
│   └─────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────┤
│ RESULTS REGION (offset: results_offset)                             │
│   [Agent 0 results] [Agent 1 results] ... [Agent N-1 results]       │
│   Each slot: test-specific result data (LatencyStats, etc.)         │
└─────────────────────────────────────────────────────────────────────┘
```

```cpp
// Constants
constexpr size_t MAX_CONCURRENT_PAIRS = 16;
constexpr size_t AGENT_SLOT_SIZE = 4096;        // Metadata per agent
constexpr size_t AGENT_STATUS_SIZE = 64;        // Status per agent
constexpr size_t AGENT_RESULT_SIZE = 64 * 1024; // 64KB results per agent

struct ActivePair {
    uint32_t agent_a;
    uint32_t agent_b;
};

enum class CommandType : uint32_t {
    IDLE = 0,
    CONFIGURE,        // Load test_config, prepare buffers
    START,            // Begin test execution
    STOP,             // Abort test
    COLLECT,          // Upload minimal results to Controller buffer
    FETCH_FULL_STATS  // Prepare full stats in agent buffer for NIXL read
};

enum class AgentState : uint32_t {
    IDLE = 0,
    READY,            // Configured and waiting
    RUNNING,          // Test in progress
    DONE,             // Test complete, results ready
    ERROR,            // Error occurred
    STATS_READY       // Full stats serialized in agent buffer
};
```

---

#### Orchestrator Interface

The Controller uses an abstract orchestrator interface to spawn agent containers. This allows pluggable backends (Docker, Kubernetes, etc.).

```cpp
// Abstract interface for container orchestration
class Orchestrator {
public:
    virtual ~Orchestrator() = default;

    // Spawn an agent container with specified environment variables
    virtual void spawn_agent(
        uint32_t agent_id,
        const map<string, string>& env_vars
    ) = 0;

    // Wait for all spawned containers to be running
    virtual bool wait_for_agents_running(uint64_t timeout_ms) = 0;

    // Terminate all agent containers
    virtual void terminate_all_agents() = 0;
};

// Docker implementation
class DockerOrchestrator : public Orchestrator {
    string image_name_;
    vector<string> container_ids_;

public:
    DockerOrchestrator(const string& image) : image_name_(image) {}

    void spawn_agent(uint32_t agent_id, const map<string, string>& env_vars) override {
        // Use Docker SDK/API to create and start container
        // docker run -e CTRL_ENDPOINT=... -e AGENT_ID=... --network host --privileged <image>
        string container_id = docker_create_container(image_name_, env_vars);
        docker_start_container(container_id);
        container_ids_.push_back(container_id);
    }

    bool wait_for_agents_running(uint64_t timeout_ms) override {
        // Poll container status until all are running
        // ...
    }

    void terminate_all_agents() override {
        for (const auto& id : container_ids_) {
            docker_stop_container(id);
            docker_remove_container(id);
        }
    }
};
```

#### Initialization

```cpp
void Controller::initialize(uint32_t num_agents, Orchestrator& orchestrator) {
    num_agents_ = num_agents;
    orchestrator_ = &orchestrator;

    // 1. Calculate buffer size
    size_t header_size = sizeof(BufferHeader);
    size_t agent_slots_size = num_agents * AGENT_SLOT_SIZE;
    size_t test_ctrl_size = sizeof(TestCommand) + num_agents * AGENT_STATUS_SIZE;
    size_t notification_size = num_agents * NOTIFICATION_SLOT_SIZE;
    size_t results_size = num_agents * AGENT_RESULT_SIZE;
    size_t total_size = header_size + agent_slots_size + test_ctrl_size +
                        notification_size + results_size;

    // 2. Allocate and register with NIXL
    buffer_ = allocate_registered_memory(total_size);
    buffer_desc_ = nixl_register_buffer(endpoint_, buffer_, total_size);

    // 3. Initialize header
    auto* header = reinterpret_cast<BufferHeader*>(buffer_);
    header->num_agents = num_agents;
    header->agent_slots_offset = header_size;
    header->test_ctrl_offset = header_size + agent_slots_size;
    header->notification_offset = header_size + agent_slots_size + test_ctrl_size;
    header->results_offset = header_size + agent_slots_size + test_ctrl_size + notification_size;
    header->ready_flag = 0;

    // 4. Initialize test command to IDLE
    auto* cmd = get_test_command();
    cmd->command_seq = 0;
    cmd->command_type = CommandType::IDLE;

    // 5. Serialize NIXL endpoint + buffer metadata for agents
    string endpoint_b64 = base64_encode(serialize_endpoint(endpoint_));
    string buffer_b64 = base64_encode(serialize_buffer(buffer_desc_));

    // 6. Initialize notifiers for agent status slots
    initialize_notifications();

    // 7. Spawn agent containers via orchestrator
    spawn_agents(endpoint_b64, buffer_b64);
}

void Controller::spawn_agents(const string& endpoint_b64, const string& buffer_b64) {
    LOG_INFO("Spawning {} agent containers", num_agents_);

    for (uint32_t i = 0; i < num_agents_; i++) {
        map<string, string> env_vars = {
            {"CTRL_ENDPOINT", endpoint_b64},
            {"CTRL_BUFFER", buffer_b64},
            {"AGENT_ID", std::to_string(i)},
            {"NUM_AGENTS", std::to_string(num_agents_)}
        };
        // Note: Agent computes its own slot offsets by reading BufferHeader

        orchestrator_->spawn_agent(i, env_vars);
        LOG_DEBUG("Spawned agent {} container", i);
    }

    // Wait for containers to start
    if (!orchestrator_->wait_for_agents_running(SPAWN_TIMEOUT_MS)) {
        throw std::runtime_error("Timeout waiting for agent containers to start");
    }

    LOG_INFO("All {} agent containers running", num_agents_);
}
```

---

#### Rendezvous Protocol

**No TCP required.** Controller passes NIXL metadata via environment variables when spawning agent containers.

```
┌─────────────────────────────────────────────────────────────────────┐
│ CONTROLLER                              AGENTS                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ 1. Initialize NIXL, allocate buffer                                 │
│                                                                     │
│ 2. Spawn agents via orchestrator with env vars:                     │
│    CTRL_ENDPOINT=<base64>                                           │
│    CTRL_BUFFER=<base64>                                             │
│    AGENT_ID=0,1,2...                                                │
│    NUM_AGENTS=N                                                     │
│         │                                                           │
│         └─────── Docker/K8s API ──────────────────▶ Containers start│
│                                                                     │
│ 3. Initialize notifiers for agent slots                             │
│                                                    Agent initializes │
│                                                    Parses env vars   │
│                                                    Connects to ctrl  │
│                                                    Reads BufferHeader│
│                                                    Computes offsets  │
│                                                           │         │
│         ┌════════ NIXL Write (metadata) ═══════════════════┘        │
│         ▼                                                           │
│ 4. Receive notification: agent N registered                         │
│    (repeat for all N agents)                                        │
│                                                                     │
│ 5. All agents registered → set ready_flag                           │
│    Notify all agents: RENDEZVOUS_COMPLETE                           │
│         │                                                           │
│         └════════ NIXL Write (notifications) ══════▶ Agents wake    │
│                                                      Read peer data │
│                                                      Enter event loop│
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**Rendezvous Completion (Notification-Based):**
```cpp
void Controller::wait_for_rendezvous() {
    std::set<uint32_t> pending;
    for (uint32_t i = 0; i < num_agents_; i++) {
        pending.insert(i);
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(RENDEZVOUS_TIMEOUT_MS);

    while (!pending.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw std::runtime_error("Rendezvous timeout: agents not registered");
        }

        auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();

        // Wait for notifications from agent slot writes
        bool any_notified = wait_any_agent_notification(pending, remaining_ms);

        if (any_notified) {
            // Check which agents have populated their slots
            for (auto it = pending.begin(); it != pending.end(); ) {
                uint32_t id = *it;
                auto* slot = get_agent_slot(id);
                if (slot->populated_flag != 0) {
                    LOG_INFO("Agent {} registered", id);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    LOG_INFO("All {} agents registered, completing rendezvous", num_agents_);

    // Set ready flag
    auto* header = get_header();
    header->ready_flag = 1;

    // Notify all agents that rendezvous is complete
    notify_all_agents(NotificationType::RENDEZVOUS_COMPLETE);
}
```

---

### 2.b Manage Performance Tests

**All communication uses NIXL memory exclusively. No TCP/sockets anywhere in the system.**

#### TestCommand Structure

```cpp
struct TestCommand {
    uint64_t command_seq;               // Monotonically increasing
    CommandType command_type;
    TestConfig test_config;

    // Support concurrent pairs (for CONCURRENT_BANDWIDTH)
    uint32_t num_active_pairs;
    ActivePair active_pairs[MAX_CONCURRENT_PAIRS];
};
```

#### Test Orchestration Flow (Notification-Based)

```
┌─────────────────────────────────────────────────────────────────────┐
│ CONTROLLER                              AGENTS                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ 1. Write TestCommand:                                               │
│    command_seq++                        (waiting on notification)   │
│    command_type = CONFIGURE                     │                   │
│    test_config = {...}                          │                   │
│    active_pairs = [(0,1)]                       │                   │
│         │                                       │                   │
│         └──────── NIXL buffer ─────────────────▶│                   │
│    Notify all agents: COMMAND_READY             │                   │
│         └════════ NIXL notify ═════════════════▶│                   │
│                                                 ▼                   │
│                                         2. Agents wake on notify    │
│                                            Read test_config         │
│                                            If in active_pairs:      │
│                                              Prepare buffers        │
│                                              Write status = READY   │
│         ┌════════ NIXL notify ◀════════════════┘                   │
│         ▼                                                           │
│ 3. Receive status notifications                                     │
│    Wait for all active agents READY                                 │
│         │                                                           │
│ 4. Write TestCommand:                                               │
│    command_seq++                                                    │
│    command_type = START                         │                   │
│    Notify active pair: COMMAND_READY            │                   │
│         └════════ NIXL notify ═════════════════▶│                   │
│                                                 ▼                   │
│                                         5. Active agents wake:      │
│                                            Write status = RUNNING   │
│                                            Execute test             │
│                                            Write status = DONE      │
│         ┌════════ NIXL notify ◀════════════════┘                   │
│         ▼                                                           │
│ 6. Receive DONE notifications                                       │
│         │                                                           │
│ 7. Write TestCommand:                                               │
│    command_type = COLLECT                       │                   │
│    Notify all agents: COMMAND_READY             │                   │
│         └════════ NIXL notify ═════════════════▶│                   │
│                                                 ▼                   │
│                                         8. Agents wake, write       │
│                                            results to results region│
│         ┌════════ NIXL notify ◀════════════════┘                   │
│         ▼                                                           │
│ 9. Receive completion notifications                                 │
│    Read results from results region                                 │
│    Process and store                                                │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

#### Controller Test Manager

```cpp
class TestManager {
    ControllerBuffer* buffer_;

    void run_pairwise_test(TestType type, const vector<pair<uint32_t,uint32_t>>& pairs) {
        auto* cmd = buffer_->get_test_command();

        // 1. Configure all agents for this test type
        cmd->command_seq++;
        cmd->command_type = CommandType::CONFIGURE;
        cmd->test_config.type = type;
        // ... fill test_config
        wait_for_all_agents_state(AgentState::READY);

        // 2. Execute all pairs
        for (const auto& pair : pairs) {
            cmd->command_seq++;
            cmd->num_active_pairs = 1;
            cmd->active_pairs[0] = {pair.first, pair.second};
            cmd->command_type = CommandType::START;

            // Wait for this pair to complete
            wait_for_agents_done({pair.first, pair.second});
        }

        // 3. Collect results ONCE after all pairs complete (Option B)
        cmd->command_seq++;
        cmd->command_type = CommandType::COLLECT;
        cmd->num_active_pairs = 0;  // All agents with results should upload
        wait_for_all_initiators_done();

        // 4. Read and aggregate results from Controller buffer
        aggregate_results();
    }

    void wait_for_agents_done(const vector<uint32_t>& agent_ids) {
        while (true) {
            bool all_done = true;
            for (uint32_t id : agent_ids) {
                auto* status = buffer_->get_agent_status(id);
                if (status->state != AgentState::DONE) {
                    all_done = false;
                    break;
                }
            }
            if (all_done) break;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    void wait_for_all_agents_state(AgentState expected) {
        while (true) {
            bool all_ready = true;
            for (uint32_t id = 0; id < num_agents_; id++) {
                auto* status = buffer_->get_agent_status(id);
                if (status->state != expected) {
                    all_ready = false;
                    break;
                }
            }
            if (all_ready) break;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
};
```

#### Agent Test Loop

```cpp
void Agent::test_loop() {
    uint64_t last_seen_seq = 0;

    while (running_) {
        // Poll for new command
        TestCommand cmd = nixl_read_test_command(controller_buffer_);

        if (cmd.command_seq == last_seen_seq) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        last_seen_seq = cmd.command_seq;

        switch (cmd.command_type) {
            case CommandType::CONFIGURE:
                handle_configure(cmd.test_config);
                accumulated_results_.clear();  // Reset for new test type
                update_status(AgentState::READY);
                break;

            case CommandType::START:
                if (am_i_active(cmd)) {
                    update_status(AgentState::RUNNING);
                    auto result = execute_test(cmd);
                    accumulated_results_.push_back(result);  // Store locally
                    update_status(AgentState::DONE);
                }
                break;

            case CommandType::COLLECT:
                // All initiators upload their accumulated results
                if (has_results()) {
                    write_minimal_results_to_controller();
                    update_status(AgentState::DONE);
                }
                break;

            case CommandType::FETCH_FULL_STATS:
                // Serialize full stats to agent buffer for controller to read
                serialize_full_stats_to_buffer();
                update_status(AgentState::STATS_READY);
                break;

            case CommandType::STOP:
                abort_test();
                update_status(AgentState::IDLE);
                break;
        }

        // Acknowledge command
        update_last_ack_seq(cmd.command_seq);
    }
}

void Agent::serialize_full_stats_to_buffer() {
    // Write full statistics to FULL_STATS_REGION of agent buffer
    size_t stats_offset = config_.buffer_size() - FULL_STATS_REGION_SIZE;
    void* stats_region = static_cast<uint8_t*>(test_buffer_) + stats_offset;

    // Serialize all accumulated full results
    size_t written = 0;
    for (const auto& result : accumulated_full_results_) {
        memcpy(static_cast<uint8_t*>(stats_region) + written,
               &result, sizeof(result));
        written += sizeof(result);
    }
    // Write header with count
    // Controller can now read this region via NIXL
}

bool Agent::am_i_active(const TestCommand& cmd) {
    for (uint32_t i = 0; i < cmd.num_active_pairs; i++) {
        if (cmd.active_pairs[i].agent_a == my_id_ ||
            cmd.active_pairs[i].agent_b == my_id_) {
            return true;
        }
    }
    return false;
}
```

### 2.c Collect and Collate Performance Data

**Data Collection:**
- Each agent writes results to designated region in Controller buffer
- Controller polls for completion flags
- Aggregates raw measurements from all agents

**Collation & Analysis:**
```
struct CollatedResults {
    // Per-pair metrics
    map<pair<agent_id, agent_id>, PerfSummary> pair_metrics;

    // Aggregate metrics
    double min_bandwidth;
    double max_bandwidth;
    double avg_bandwidth;
    uint64_t p50_latency;
    uint64_t p99_latency;

    // Anomaly detection
    vector<pair<agent_id, agent_id>> slow_links;
};
```

**Output Formats:**
- Console summary
- Detailed CSV export
- JSON for programmatic consumption
- Input to topology graph construction

---