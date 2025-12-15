# Cluster Topology Discovery & Performance Measurement System

A C++ distributed agent system that measures memory transfer performance between cluster nodes using the NVIDIA NIXL framework.

---

## Assumptions & Limitations

### Environment Assumptions

| Assumption | Description |
|------------|-------------|
| **Trusted network** | All agents and controller run in a secured cluster. No authentication between components. See Section 6.a for optional security enhancements. |
| **Homogeneous agents** | All agents have similar capabilities (memory, NIXL support). Heterogeneous configurations not tested. |
| **Stable network** | Network topology doesn't change during test execution. Dynamic topology changes may cause incorrect results. |
| **NIXL availability** | NIXL SDK installed and functional on all nodes. UCX available as transport layer. |
| **Sufficient memory** | Each agent can allocate 256MB (default) of registered memory. Controller can allocate ~68KB × N agents. |

### NIXL/UCX Assumptions

| Assumption | Description |
|------------|-------------|
| **Write visibility** | `nixl_write` completion guarantees data is visible to remote reader. If NIXL does not provide this ordering, explicit memory barriers are required. |
| **Completion semantics** | `nixl_wait` returns only after remote memory is updated. |
| **Error reporting** | NIXL reports transient vs. permanent errors distinctly for retry logic. |
| **Event notification** | UCX `ucp_worker_arm`/`ucp_worker_get_efd` available for event-driven notifications. Falls back to polling if unavailable. |

### Design Limitations

| Limitation | Impact | Workaround |
|------------|--------|------------|
| **Fixed agent count** | Number of agents must be known at controller startup. Cannot dynamically add/remove agents. | Restart controller with new count. |
| **No controller HA** | Single controller is a single point of failure. If controller crashes, all state is lost. | Restart entire test session. |
| **Sequential pair testing** | Most tests run one pair at a time to avoid interference. O(N²) pairs can take hours for large N. | Use Level 1 (latency only) for quick results. |
| **No checkpointing** | Test progress not persisted. Crash loses all partial results. | Phase 2 error handling mitigates but doesn't eliminate. |
| **Full mesh connectivity** | All agents must be able to reach all other agents via NIXL. Partitioned networks not supported. | Ensure full connectivity before testing. |

### Scalability

| Parameter | Tested Range | Notes |
|-----------|--------------|-------|
| Agent count | 4 - 64 | Design target. Larger scales may work but untested. |
| Max agents (theoretical) | ~1000 | Limited by controller buffer size and O(N²) test time. |

**Test Duration Estimates (N=64, 2016 pairs):**

| Level | Measurement Time | Coordination | Total (Async) | Total (Polling Fallback) |
|-------|------------------|--------------|---------------|--------------------------|
| Level 1 | ~22s (11K iter × 1µs RTT) | Near-zero | **2-5 minutes** | ~10-15 minutes |
| Level 2 | ~30-60 min (BW tests) | Near-zero | **30-60 minutes** | ~1-2 hours |
| Level 3 | ~4-8 hours (concurrent) | Near-zero | **4-8 hours** | ~8-16 hours |

*Note: Async notifications eliminate inter-pair coordination delays. Polling fallback adds ~100-400µs per pair transition.*

**Memory Requirements:**

```
Agent memory:
  Test buffer = window_size × max_message_size
              = 64 × 4MB = 256MB (default)

Controller memory:
  Header:        64 bytes
  Agent slots:   N × 4KB
  Test control:  sizeof(TestCommand) + N × 64 bytes ≈ 1KB + N × 64
  Results:       N × 64KB

  Total ≈ N × 68KB + overhead

  Examples:
    N=64:   ~4.4 MB
    N=256:  ~17.4 MB
    N=1000: ~68 MB
```

### Clock & Timing

| Aspect | Requirement |
|--------|-------------|
| **Clock synchronization** | Not required for RTT/2 latency measurement. |
| **TSC calibration** | Recommended if using `rdtsc()` for timing. |
| **PTP/NTP** | Only needed if one-way latency measurement added in future. |
| **Timer resolution** | Nanosecond resolution required (`std::chrono::steady_clock` or calibrated TSC). |

### Known Trade-offs

| Decision | Trade-off |
|----------|-----------|
| **Upfront buffer allocation** | Uses more memory but avoids per-test synchronization overhead. |
| **Notification-based coordination** | More complex than polling but lower CPU usage and better scalability. Polling fallback available. |
| **Sequential pair testing** | Accurate measurements but slow. Parallel testing would be faster but less accurate. |
| **Initiator-only result upload** | Reduces data duplication but loses responder's perspective. |
| **TCP for bootstrap only** | Requires well-known controller address. No service discovery. |

---

## 1. Agent Design

### 1.a Bootstrap

Each agent runs as a standalone process on a cluster node. On startup:

1. Agent initializes NIXL runtime
2. Allocates and registers test buffer (see buffer design below)
3. Serializes NIXL endpoint metadata + buffer descriptor into blobs
4. Connects to Controller at well-known IP/port (TCP handshake)
5. Receives from Controller:
   - Assigned agent ID (unique integer)
   - Controller's NIXL endpoint metadata blob
   - Controller's registered buffer blob
6. Computes its slot offset in Controller buffer
7. Writes its own endpoint + buffer blobs into assigned slot via NIXL transfer
8. Polls Controller buffer `ready_flag` until rendezvous complete
9. Extracts all peer endpoint metadata and buffer information from Controller buffer
10. Establishes NIXL connections to all peer agents
11. Enters test loop (polls for commands from Controller)

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

public:
    void initialize(const AgentConfig& config) {
        config_ = config;

        // 1. Init NIXL
        endpoint_ = nixl_create_endpoint();

        // 2. Allocate and register test buffer (size from config)
        test_buffer_size_ = config_.buffer_size();
        test_buffer_ = allocate_registered_memory(test_buffer_size_);
        test_buffer_desc_ = nixl_register_buffer(endpoint_, test_buffer_, test_buffer_size_);

        LOG_INFO("Agent buffer allocated: {} MB ({}x {} byte slots)",
                 test_buffer_size_ / (1024*1024),
                 config_.window_size,
                 config_.max_message_size);

        // 3. TCP handshake with controller (get ID, controller blobs)
        tcp_handshake();

        // 4. Write our metadata to controller buffer via NIXL
        write_metadata_to_controller();

        // 5. Wait for rendezvous completion
        wait_for_ready_flag();

        // 6. Read all peer metadata
        read_peer_metadata();

        // 7. Enter test loop
        test_loop();
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

The Controller orchestrates agent discovery and connection establishment.

**Design Principle:** TCP used ONLY for initial handshake. All subsequent communication via NIXL memory.

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

#### Initialization

```cpp
void Controller::initialize(uint32_t num_agents) {
    // 1. Calculate buffer size
    size_t header_size = sizeof(BufferHeader);
    size_t agent_slots_size = num_agents * AGENT_SLOT_SIZE;
    size_t test_ctrl_size = sizeof(TestCommand) + num_agents * AGENT_STATUS_SIZE;
    size_t results_size = num_agents * AGENT_RESULT_SIZE;
    size_t total_size = header_size + agent_slots_size + test_ctrl_size + results_size;

    // 2. Allocate and register with NIXL
    buffer_ = allocate_registered_memory(total_size);

    // 3. Initialize header
    auto* header = reinterpret_cast<BufferHeader*>(buffer_);
    header->num_agents = num_agents;
    header->agent_slots_offset = header_size;
    header->test_ctrl_offset = header_size + agent_slots_size;
    header->results_offset = header_size + agent_slots_size + test_ctrl_size;
    header->ready_flag = 0;

    // 4. Initialize test command to IDLE
    auto* cmd = get_test_command();
    cmd->command_seq = 0;
    cmd->command_type = CommandType::IDLE;

    // 5. Serialize NIXL endpoint + buffer metadata
    nixl_endpoint_blob_ = serialize_endpoint();
    buffer_blob_ = serialize_buffer_descriptor(buffer_, total_size);

    // 6. Start TCP listener for initial handshake ONLY
    start_tcp_listener(listen_port_);
}
```

---

#### Rendezvous Protocol

**TCP Handshake (one-time per agent):**
```
Controller                              Agent
    |                                      |
    |<-------- TCP Connect ----------------|
    |                                      |
    |-- agent_id ------------------------->|  (assigned ID)
    |-- nixl_endpoint_blob --------------->|  (controller's NIXL endpoint)
    |-- buffer_blob ---------------------->|  (controller's buffer descriptor)
    |                                      |
    |         [TCP connection closed]      |
    |                                      |
```

**NIXL Rendezvous (after TCP handshake):**
```
Controller                              Agent
    |                                      |
    | (polls agent slots)                  | (deserializes blobs)
    |                                      |
    |<== NIXL Write (agent metadata) ======|  (agent writes to its slot)
    |                                      |
    | (detects populated_flag)             |
    |                                      |
```

**Rendezvous Completion:**
```cpp
void Controller::wait_for_rendezvous() {
    while (true) {
        bool all_populated = true;
        for (uint32_t i = 0; i < num_agents_; i++) {
            auto* slot = get_agent_slot(i);
            if (slot->populated_flag == 0) {
                all_populated = false;
                break;
            }
        }
        if (all_populated) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Signal ready via NIXL (agents poll this)
    auto* header = get_header();
    header->ready_flag = 1;
}

// Agent side: poll for ready_flag
void Agent::wait_for_ready() {
    while (true) {
        uint64_t ready = nixl_read_u64(controller_buffer_,
                                        offsetof(BufferHeader, ready_flag));
        if (ready != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Now read all peer metadata from agent slots
    read_peer_metadata();
}
```

---

### 2.b Manage Performance Tests

**All test orchestration uses NIXL memory - no TCP after rendezvous.**

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

#### Test Orchestration Flow (NIXL Only)

```
┌─────────────────────────────────────────────────────────────────────┐
│ CONTROLLER                              AGENTS                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ 1. Write TestCommand:                                               │
│    command_seq++                        (polling test_ctrl region)  │
│    command_type = CONFIGURE                     │                   │
│    test_config = {...}                          │                   │
│    active_pairs = [(0,1)]                       │                   │
│         │                                       │                   │
│         └──────── NIXL buffer ─────────────────▶│                   │
│                                                 ▼                   │
│                                         2. Agents detect new seq    │
│                                            Read test_config         │
│                                            If in active_pairs:      │
│                                              Prepare buffers        │
│                                              Write status = READY   │
│                                              Write last_ack_seq     │
│         ┌──────── NIXL buffer ◀────────────────┘                   │
│         ▼                                                           │
│ 3. Poll agent status slots                                          │
│    Wait for all active agents READY                                 │
│         │                                                           │
│ 4. Write TestCommand:                                               │
│    command_seq++                                                    │
│    command_type = START                         │                   │
│         │                                       │                   │
│         └──────── NIXL buffer ─────────────────▶│                   │
│                                                 ▼                   │
│                                         5. Active agents:           │
│                                            Write status = RUNNING   │
│                                            Execute test             │
│                                            Write status = DONE      │
│         ┌──────── NIXL buffer ◀────────────────┘                   │
│         ▼                                                           │
│ 6. Poll for DONE status                                             │
│         │                                                           │
│ 7. Write TestCommand:                                               │
│    command_type = COLLECT                       │                   │
│         └──────── NIXL buffer ─────────────────▶│                   │
│                                                 ▼                   │
│                                         8. Agents write results     │
│                                            to results region        │
│         ┌──────── NIXL buffer ◀────────────────┘                   │
│         ▼                                                           │
│ 9. Read results from results region                                 │
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

## 3. Topology Discovery

**Goal:** Infer approximate physical interconnect topology from collected performance data.

### 3.a Graph Construction

#### Graph Model: G(V, HV, E)

```cpp
struct TopologyGraph {
    // V: Physical nodes (agents in the cluster)
    vector<PhysicalNode> V;

    // HV: Virtual/hidden nodes (inferred switches, shared links)
    vector<HiddenNode> HV;

    // E: Edges connecting V and HV nodes
    vector<Edge> E;
};

struct PhysicalNode {
    uint32_t agent_id;
    string hostname;
    NodeType type;              // GPU, CPU, NIC
    uint32_t tier;              // Assigned topology tier (0 = closest)
};

struct HiddenNode {
    uint32_t hv_id;             // Unique ID for hidden node
    HiddenNodeType type;        // SWITCH, SHARED_LINK, BOTTLENECK
    uint32_t tier;              // Topology tier level
    string inferred_label;      // e.g., "NVSwitch-0", "ToR-Switch-1"
    double confidence;          // 0.0-1.0, inference confidence
};

enum class HiddenNodeType {
    NVSWITCH,                   // Inferred NVSwitch (intra-node GPU interconnect)
    TOR_SWITCH,                 // Top-of-Rack switch
    SPINE_SWITCH,               // Spine/aggregation switch
    SHARED_LINK,                // Shared bandwidth bottleneck
    UNKNOWN_BOTTLENECK          // Detected but unclassified
};

struct Edge {
    uint32_t src_id;            // Node ID (physical or hidden)
    uint32_t dst_id;
    bool src_is_hidden;
    bool dst_is_hidden;

    // Edge properties
    double bandwidth_gbps;      // Measured or inferred capacity
    uint64_t latency_ns;        // One-way latency
    double utilization;         // Observed utilization (0.0-1.0)
    EdgeType type;              // DIRECT, SWITCHED, SHARED
};
```

---

#### Incremental Topology Discovery

The algorithm supports **incremental refinement** - produce a useful topology graph quickly with minimal data, then enrich as more measurements become available.

| Level | Test Required | Graph Output | Use Case |
|-------|---------------|--------------|----------|
| **Level 1: Quick** | `PAIRWISE_LATENCY` only | Topology structure, tiers, inferred switches, **default** bandwidth estimates | Fast cluster overview (~minutes) |
| **Level 2: Measured** | + `BANDWIDTH` | Above + **measured** edge capacities | Accurate capacity planning |
| **Level 3: Full** | + `CONCURRENT_BANDWIDTH` | Above + shared bottleneck detection | Complete topology with contention analysis |

**Time Estimates (N agents):**
- Level 1: O(N²) pairs × 11K iterations × ~1µs = ~minutes for 64 nodes
- Level 2: O(N²) pairs × multiple sizes × 100 iterations = ~10-30 minutes
- Level 3: O(N²) pair combinations × 50 iterations = ~hours

**Recommendation:** Run Level 1 first for quick topology overview. Run Level 2/3 overnight or as needed.

---

#### Algorithm Overview

The graph construction proceeds in four phases. **Only Phase 1-2 require data; Phase 3-4 use optional data for refinement.**

```
┌─────────────────────────────────────────────────────────────────┐
│  Input: Latency Matrix L[N][N] (required)                       │
│         Bandwidth Matrix B[N][N] (optional, for Level 2+)       │
│         ConcurrentTestResults (optional, for Level 3)           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 1: Hierarchical Clustering                               │
│  - Cluster nodes by latency similarity                          │
│  - Identify topology tiers (intra-node, intra-rack, inter-rack) │
│  Output: Tier assignments, cluster membership                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 2: Hidden Node Inference                                 │
│  - Infer switch nodes as cluster ancestors                      │
│  - Use latency triangulation to detect intermediaries           │
│  Output: HV nodes with tier assignments                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 3: Edge Construction & Capacity Estimation               │
│  - Connect V nodes to HV nodes                                  │
│  - Estimate edge bandwidth from measurements                    │
│  Output: Edge set E with bandwidth labels                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 4: Shared Bottleneck Detection (Optional)                │
│  - Concurrent transfer correlation analysis                     │
│  - Refine HV nodes for shared links                             │
│  Output: Updated graph with bottleneck annotations              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Output: TopologyGraph G(V, HV, E)                              │
└─────────────────────────────────────────────────────────────────┘
```

---

#### Phase 1: Hierarchical Clustering

**Input:** Latency matrix `L[N][N]` from `PAIRWISE_LATENCY` test

**Algorithm:** Agglomerative hierarchical clustering with single-linkage

```cpp
// Latency-based distance metric
double distance(uint32_t i, uint32_t j, const LatencyMatrix& L) {
    // Use average of both directions for symmetry
    return (L[i][j].avg_ns + L[j][i].avg_ns) / 2.0;
}

struct Dendrogram {
    vector<ClusterNode> nodes;
    // nodes[0..N-1] are leaves (physical nodes)
    // nodes[N..] are internal nodes (merges)
};

struct ClusterNode {
    uint32_t id;
    uint32_t left_child;        // -1 if leaf
    uint32_t right_child;       // -1 if leaf
    double merge_distance;      // Latency at which clusters merged
    vector<uint32_t> members;   // All leaf nodes in this cluster
};

Dendrogram build_dendrogram(const LatencyMatrix& L) {
    // Standard agglomerative clustering
    // 1. Initialize each node as its own cluster
    // 2. Find pair of clusters with minimum distance
    // 3. Merge into new cluster, record merge distance
    // 4. Repeat until single cluster remains
}
```

**Tier Assignment:** Cut dendrogram at configurable thresholds

```cpp
struct TierConfig {
    // Latency thresholds (nanoseconds) - configurable per deployment
    uint64_t tier0_max_ns = 5000;      // < 5µs: intra-node (NVLink/PCIe)
    uint64_t tier1_max_ns = 15000;     // < 15µs: intra-rack (NVSwitch/ToR)
    uint64_t tier2_max_ns = 50000;     // < 50µs: inter-rack (spine)
    // > 50µs: inter-pod / WAN
};

vector<vector<uint32_t>> assign_tiers(
    const Dendrogram& D,
    const TierConfig& config
) {
    vector<vector<uint32_t>> tiers;  // tiers[t] = list of cluster IDs at tier t

    // Walk dendrogram, cut at each threshold
    // Nodes merged below threshold T are in same tier-T cluster
}
```

**Output:**
- `tier_assignment[node_id]` → tier number (0, 1, 2, ...)
- `cluster_membership[node_id]` → cluster ID within tier
- `dendrogram` → full hierarchical structure

---

#### Phase 2: Hidden Node Inference

**Goal:** Infer switch/interconnect nodes that explain the clustering structure

**Principle:** If nodes {A, B, C} form a cluster with similar low latency, they likely connect through a common switch (hidden node).

```cpp
vector<HiddenNode> infer_hidden_nodes(
    const Dendrogram& D,
    const vector<vector<uint32_t>>& tier_clusters,
    const TierConfig& config
) {
    vector<HiddenNode> HV;
    uint32_t hv_id = 0;

    // For each non-trivial cluster, infer a hidden switch node
    for (uint32_t tier = 0; tier < tier_clusters.size(); tier++) {
        for (const auto& cluster : tier_clusters[tier]) {
            if (cluster.members.size() > 1) {
                HiddenNode hv;
                hv.hv_id = hv_id++;
                hv.tier = tier;
                hv.type = classify_hidden_node(tier, cluster.members.size());
                hv.inferred_label = generate_label(hv.type, tier, hv.hv_id);
                hv.confidence = compute_confidence(cluster, D);
                HV.push_back(hv);
            }
        }
    }
    return HV;
}

HiddenNodeType classify_hidden_node(uint32_t tier, size_t cluster_size) {
    switch (tier) {
        case 0:
            // Tier 0 with multiple GPUs → likely NVSwitch
            return cluster_size <= 8 ? HiddenNodeType::NVSWITCH
                                      : HiddenNodeType::TOR_SWITCH;
        case 1:
            return HiddenNodeType::TOR_SWITCH;
        case 2:
            return HiddenNodeType::SPINE_SWITCH;
        default:
            return HiddenNodeType::UNKNOWN_BOTTLENECK;
    }
}
```

**Latency Triangulation (refinement):**

Detect if node B lies on path between A and C:

```cpp
bool is_on_path(uint32_t A, uint32_t B, uint32_t C, const LatencyMatrix& L) {
    double lat_AC = L[A][C].avg_ns;
    double lat_AB = L[A][B].avg_ns;
    double lat_BC = L[B][C].avg_ns;

    // If latency(A,C) ≈ latency(A,B) + latency(B,C), B is on the path
    double tolerance = 0.15;  // 15% tolerance
    double expected = lat_AB + lat_BC;
    return abs(lat_AC - expected) / expected < tolerance;
}
```

This helps refine the tree structure when simple clustering is ambiguous.

---

#### Phase 3: Edge Construction & Capacity Estimation

**Supports incremental discovery:**
- **Level 1 (no bandwidth data):** Uses tier-based default capacities
- **Level 2+ (with bandwidth data):** Uses measured capacities

**Connect physical nodes to hidden nodes:**

```cpp
vector<Edge> construct_edges(
    const vector<PhysicalNode>& V,
    const vector<HiddenNode>& HV,
    const LatencyMatrix& L,
    const BandwidthMatrix& B  // From Phase 2 tests, if available
) {
    vector<Edge> E;

    // Connect each physical node to its cluster's hidden node
    for (const auto& v : V) {
        uint32_t hv_id = get_cluster_hidden_node(v.agent_id);
        if (hv_id != INVALID) {
            Edge e;
            e.src_id = v.agent_id;
            e.dst_id = hv_id;
            e.src_is_hidden = false;
            e.dst_is_hidden = true;

            // Estimate edge latency: half of intra-cluster latency
            e.latency_ns = estimate_edge_latency(v.agent_id, hv_id, L);

            // Bandwidth: use max observed to any peer in same cluster
            e.bandwidth_gbps = estimate_edge_bandwidth(v.agent_id, B);

            e.type = EdgeType::SWITCHED;
            E.push_back(e);
        }
    }

    // Connect hidden nodes hierarchically (switch to spine, etc.)
    for (size_t i = 0; i < HV.size(); i++) {
        uint32_t parent_hv = find_parent_hidden_node(HV[i]);
        if (parent_hv != INVALID) {
            Edge e;
            e.src_id = HV[i].hv_id;
            e.dst_id = parent_hv;
            e.src_is_hidden = true;
            e.dst_is_hidden = true;
            e.latency_ns = estimate_inter_tier_latency(HV[i].tier);
            e.bandwidth_gbps = estimate_aggregated_bandwidth(HV[i], B);
            e.type = EdgeType::SWITCHED;
            E.push_back(e);
        }
    }

    return E;
}
```

**Bandwidth Estimation:**

```cpp
double estimate_edge_bandwidth(uint32_t node_id, const BandwidthMatrix& B) {
    if (B.empty()) {
        // No bandwidth data, use tier-based defaults
        return get_default_bandwidth_for_tier(get_tier(node_id));
    }

    // Use maximum observed bandwidth to any peer
    // (Represents link capacity, not shared capacity)
    double max_bw = 0;
    for (uint32_t peer = 0; peer < B.size(); peer++) {
        if (peer != node_id && B[node_id][peer].bandwidth_gbps > max_bw) {
            max_bw = B[node_id][peer].bandwidth_gbps;
        }
    }
    return max_bw;
}

// Default bandwidths by tier (configurable)
double get_default_bandwidth_for_tier(uint32_t tier) {
    switch (tier) {
        case 0: return 300.0;   // NVLink: ~300-900 GB/s
        case 1: return 100.0;   // NVSwitch/ToR: ~100-400 GB/s
        case 2: return 25.0;    // Spine: ~25-100 GB/s
        default: return 10.0;
    }
}
```

---

#### Phase 4: Shared Bottleneck Detection (Level 3 Only)

**Goal:** Identify hidden shared links by observing bandwidth degradation under concurrent load.

**Only runs for Level 3 topology discovery.** Skipped if `ConcurrentTestResults` not available.

**Requires:** `CONCURRENT_BANDWIDTH` test from section 1.c, which produces `ConcurrentTestResults`.

```cpp
// Uses ConcurrentTestResult from section 1.c Memory Performance Tests

// Detect if two pairs share a bottleneck
bool shares_bottleneck(const ConcurrentTestResult& r) {
    // Significant degradation when concurrent → shared link
    double degradation1 = (r.pair1_solo_bw - r.pair1_concurrent_bw) / r.pair1_solo_bw;
    double degradation2 = (r.pair2_solo_bw - r.pair2_concurrent_bw) / r.pair2_solo_bw;

    const double threshold = 0.20;  // 20% degradation
    return degradation1 > threshold && degradation2 > threshold;
}

// Infer shared link capacity
double infer_shared_capacity(const ConcurrentTestResult& r) {
    // Total bandwidth when concurrent ≈ shared link capacity
    return r.pair1_concurrent_bw + r.pair2_concurrent_bw;
}
```

**Algorithm:**

```cpp
void detect_shared_bottlenecks(
    TopologyGraph& G,
    const vector<ConcurrentTestResult>& concurrent_results
) {
    // Build correlation graph: edge if pairs share bottleneck
    // Find connected components → each component shares a link

    vector<pair<PairID, PairID>> sharing_pairs;
    for (const auto& r : concurrent_results) {
        if (shares_bottleneck(r)) {
            sharing_pairs.push_back({r.pair1, r.pair2});
        }
    }

    // Union-find to group pairs sharing bottlenecks
    auto components = find_connected_components(sharing_pairs);

    // For each component, create SHARED_LINK hidden node
    for (const auto& component : components) {
        HiddenNode hv;
        hv.hv_id = G.HV.size();
        hv.type = HiddenNodeType::SHARED_LINK;
        hv.inferred_label = fmt::format("SharedLink-{}", hv.hv_id);

        // Estimate shared capacity
        hv.capacity_gbps = estimate_shared_capacity(component, concurrent_results);

        G.HV.push_back(hv);

        // Re-route affected edges through this bottleneck node
        update_edges_for_shared_link(G, component, hv.hv_id);
    }
}
```

---

#### Output Data Structures

```cpp
struct TopologyGraphOutput {
    TopologyGraph graph;

    // Summary statistics
    uint32_t num_physical_nodes;
    uint32_t num_hidden_nodes;
    uint32_t num_tiers;
    vector<uint32_t> nodes_per_tier;

    // Export methods
    string to_dot();            // GraphViz DOT format
    string to_graphml();        // GraphML XML format
    string to_json();           // JSON for programmatic use
};

// DOT format example output:
// digraph Topology {
//   // Physical nodes
//   node0 [label="GPU-0" shape=box];
//   node1 [label="GPU-1" shape=box];
//   ...
//   // Hidden nodes
//   hv0 [label="NVSwitch-0" shape=diamond style=dashed];
//   hv1 [label="ToR-0" shape=diamond style=dashed];
//   ...
//   // Edges with bandwidth labels
//   node0 -> hv0 [label="450 GB/s"];
//   node1 -> hv0 [label="450 GB/s"];
//   hv0 -> hv1 [label="100 GB/s"];
// }
```

---

#### Configuration

```cpp
struct TopologyInferenceConfig {
    // Tier thresholds (nanoseconds)
    TierConfig tier_config;

    // Clustering parameters
    enum LinkageType { SINGLE, COMPLETE, AVERAGE };
    LinkageType linkage = SINGLE;

    // Hidden node inference
    uint32_t min_cluster_size_for_switch = 2;  // Min nodes to infer switch
    double min_confidence_threshold = 0.5;      // Discard low-confidence HV

    // Shared bottleneck detection
    bool enable_bottleneck_detection = false;   // Requires Phase 2 data
    double degradation_threshold = 0.20;        // 20% BW drop = shared

    // Output
    bool emit_dot = true;
    bool emit_graphml = false;
    bool emit_json = true;
};
```

### 3.b Graph Visualization

**Export Formats:**

| Format | Use Case |
|--------|----------|
| DOT (GraphViz) | Visual rendering, debugging |
| GraphML | Interoperability, graph analysis tools |
| JSON | Programmatic consumption, web UI |

**Visualization Conventions:**

```
Physical nodes:    [box]      solid border
Hidden nodes:      <diamond>  dashed border
Tier 0 edges:      thick, green (high bandwidth)
Tier 1 edges:      medium, blue
Tier 2+ edges:     thin, gray
Bottleneck edges:  red, annotated with capacity
```

**Example Rendering:**

```
                    ┌─────────────┐
                    │  Spine-0    │ (HV, Tier 2)
                    └──────┬──────┘
                           │ 25 GB/s
              ┌────────────┴────────────┐
              │                         │
        ┌─────┴─────┐             ┌─────┴─────┐
        │   ToR-0   │             │   ToR-1   │ (HV, Tier 1)
        └─────┬─────┘             └─────┬─────┘
              │ 100 GB/s                │ 100 GB/s
      ┌───────┼───────┐         ┌───────┼───────┐
      │       │       │         │       │       │
   ┌──┴──┐ ┌──┴──┐ ┌──┴──┐   ┌──┴──┐ ┌──┴──┐ ┌──┴──┐
   │NVS-0│ │NVS-1│ │NVS-2│   │NVS-3│ │NVS-4│ │NVS-5│ (HV, Tier 0)
   └──┬──┘ └──┬──┘ └──┬──┘   └──┬──┘ └──┬──┘ └──┬──┘
      │       │       │         │       │       │
    ┌─┴─┐   ┌─┴─┐   ┌─┴─┐     ┌─┴─┐   ┌─┴─┐   ┌─┴─┐
    │0-3│   │4-7│   │8-B│     │C-F│   │...│   │...│  (Physical GPUs)
    └───┘   └───┘   └───┘     └───┘   └───┘   └───┘
              450 GB/s each
```

---

## 4. Test Harness

Containerized deployment for development, testing, and CI/CD.

### 4.a Minimal Container Deployment (Phase 1)

Basic setup to run agents and controller in containers on a single host or cluster.

#### Container Images

```dockerfile
# Dockerfile.agent
FROM nvidia/cuda:12.0-base-ubuntu22.04

RUN apt-get update && apt-get install -y \
    libibverbs-dev \
    librdmacm-dev \
    && rm -rf /var/lib/apt/lists/*

# Install NIXL SDK (assumes available)
COPY nixl-sdk/ /opt/nixl/
ENV LD_LIBRARY_PATH=/opt/nixl/lib:$LD_LIBRARY_PATH

COPY build/topo-agent /usr/local/bin/
ENTRYPOINT ["topo-agent"]
```

```dockerfile
# Dockerfile.controller
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libibverbs-dev \
    librdmacm-dev \
    && rm -rf /var/lib/apt/lists/*

COPY nixl-sdk/ /opt/nixl/
ENV LD_LIBRARY_PATH=/opt/nixl/lib:$LD_LIBRARY_PATH

COPY build/topo-controller /usr/local/bin/
ENTRYPOINT ["topo-controller"]
```

#### Minimal Docker Compose (Single Host)

```yaml
# docker-compose.yml
version: '3.8'

services:
  controller:
    build:
      context: .
      dockerfile: Dockerfile.controller
    container_name: topo-controller
    network_mode: host  # Required for RDMA/NIXL
    privileged: true    # Required for RDMA device access
    volumes:
      - /dev/infiniband:/dev/infiniband  # RDMA devices
    environment:
      - CONTROLLER_PORT=50000
      - NUM_AGENTS=4
    command: ["--port", "50000", "--agents", "4"]

  agent-0:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-0
    network_mode: host
    privileged: true
    volumes:
      - /dev/infiniband:/dev/infiniband
    depends_on:
      - controller
    environment:
      - CONTROLLER_HOST=127.0.0.1
      - CONTROLLER_PORT=50000
      - BUFFER_SIZE_MB=256
    command: ["--controller", "127.0.0.1:50000", "--buffer-mb", "256"]

  agent-1:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-1
    network_mode: host
    privileged: true
    volumes:
      - /dev/infiniband:/dev/infiniband
    depends_on:
      - controller
    command: ["--controller", "127.0.0.1:50000", "--buffer-mb", "256"]

  # Add more agents as needed (agent-2, agent-3, ...)
```

#### Running the Test Harness

```bash
# Build images
docker-compose build

# Start controller and agents
docker-compose up -d

# Check logs
docker-compose logs -f controller
docker-compose logs -f agent-0

# Run tests via controller CLI
docker exec topo-controller topo-ctl run-test --type PAIRWISE_LATENCY

# Collect results
docker exec topo-controller topo-ctl get-results --format json > results.json

# Shutdown
docker-compose down
```

#### Kubernetes Deployment (Cluster)

```yaml
# k8s/controller-deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: topo-controller
spec:
  replicas: 1
  selector:
    matchLabels:
      app: topo-controller
  template:
    metadata:
      labels:
        app: topo-controller
    spec:
      hostNetwork: true  # Required for RDMA
      containers:
      - name: controller
        image: topo-disc/controller:latest
        securityContext:
          privileged: true
        args: ["--port", "50000", "--agents", "8"]
        resources:
          limits:
            rdma/rdma_shared_device_a: 1  # RDMA device plugin
---
# k8s/agent-daemonset.yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: topo-agent
spec:
  selector:
    matchLabels:
      app: topo-agent
  template:
    metadata:
      labels:
        app: topo-agent
    spec:
      hostNetwork: true
      containers:
      - name: agent
        image: topo-disc/agent:latest
        securityContext:
          privileged: true
        args: ["--controller", "$(CONTROLLER_HOST):50000"]
        env:
        - name: CONTROLLER_HOST
          value: "controller-node-ip"
        resources:
          limits:
            rdma/rdma_shared_device_a: 1
            nvidia.com/gpu: 1  # If GPU memory testing
```

---

### 4.b Simulated Network Topologies (Phase 2)

> **Implementation Note:** This is a **later phase feature**. Implement basic containerized deployment first.

Use Docker networking and traffic control to simulate different network topologies and bandwidth constraints.

#### Docker Compose with Custom Networks

```yaml
# docker-compose.topology.yml
version: '3.8'

networks:
  # Simulate rack-local network (high bandwidth, low latency)
  rack-0:
    driver: bridge
    driver_opts:
      com.docker.network.driver.mtu: 9000  # Jumbo frames
    ipam:
      config:
        - subnet: 172.20.0.0/24

  rack-1:
    driver: bridge
    driver_opts:
      com.docker.network.driver.mtu: 9000
    ipam:
      config:
        - subnet: 172.20.1.0/24

  # Simulate cross-rack network (lower bandwidth, higher latency)
  spine:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.100.0/24

services:
  controller:
    build:
      context: .
      dockerfile: Dockerfile.controller
    networks:
      - spine
    # ...

  # Rack 0 agents
  agent-0:
    build:
      context: .
      dockerfile: Dockerfile.agent
    networks:
      - rack-0
      - spine
    # ...

  agent-1:
    networks:
      - rack-0
      - spine
    # ...

  # Rack 1 agents
  agent-2:
    networks:
      - rack-1
      - spine
    # ...

  agent-3:
    networks:
      - rack-1
      - spine
    # ...
```

#### Traffic Control for Bandwidth/Latency Simulation

```bash
#!/bin/bash
# setup-tc.sh - Apply traffic shaping to simulate network tiers

# Simulate cross-rack latency (5ms) and bandwidth limit (10Gbps)
setup_spine_tc() {
    local container=$1
    local iface="eth1"  # spine network interface

    docker exec $container tc qdisc add dev $iface root handle 1: htb default 10
    docker exec $container tc class add dev $iface parent 1: classid 1:10 htb \
        rate 10gbit ceil 10gbit
    docker exec $container tc qdisc add dev $iface parent 1:10 handle 10: \
        netem delay 5ms 0.5ms  # 5ms +/- 0.5ms jitter
}

# Simulate intra-rack (high bandwidth, low latency)
setup_rack_tc() {
    local container=$1
    local iface="eth0"  # rack network interface

    docker exec $container tc qdisc add dev $iface root handle 1: htb default 10
    docker exec $container tc class add dev $iface parent 1: classid 1:10 htb \
        rate 100gbit ceil 100gbit
    docker exec $container tc qdisc add dev $iface parent 1:10 handle 10: \
        netem delay 0.1ms  # 100us latency
}

# Apply to all containers
for i in 0 1 2 3; do
    setup_rack_tc "topo-agent-$i"
    setup_spine_tc "topo-agent-$i"
done

echo "Traffic shaping applied"
```

#### Topology Scenarios

```yaml
# scenarios/two-rack.yml
# Two racks with 4 agents each, spine interconnect

topology:
  racks:
    - name: rack-0
      agents: [0, 1, 2, 3]
      intra_rack:
        bandwidth_gbps: 100
        latency_us: 100
    - name: rack-1
      agents: [4, 5, 6, 7]
      intra_rack:
        bandwidth_gbps: 100
        latency_us: 100
  spine:
    bandwidth_gbps: 25
    latency_us: 5000  # 5ms

---
# scenarios/nvlink-cluster.yml
# Simulates NVLink-connected GPUs within nodes

topology:
  nodes:
    - name: node-0
      agents: [0, 1, 2, 3]  # 4 GPUs per node
      intra_node:
        bandwidth_gbps: 600  # NVLink
        latency_us: 2
    - name: node-1
      agents: [4, 5, 6, 7]
      intra_node:
        bandwidth_gbps: 600
        latency_us: 2
  network:
    bandwidth_gbps: 100  # InfiniBand between nodes
    latency_us: 1000
```

#### Scenario Runner

```bash
#!/bin/bash
# run-scenario.sh <scenario-file>

SCENARIO=$1

# Parse scenario and generate docker-compose
python3 scripts/gen-compose.py --scenario $SCENARIO --output docker-compose.gen.yml

# Start containers
docker-compose -f docker-compose.gen.yml up -d

# Apply traffic shaping based on scenario
python3 scripts/apply-tc.py --scenario $SCENARIO

# Run topology discovery
docker exec topo-controller topo-ctl run-test --type PAIRWISE_LATENCY

# Compare discovered topology with expected
docker exec topo-controller topo-ctl get-topology --format json > discovered.json
python3 scripts/compare-topology.py --expected $SCENARIO --discovered discovered.json

# Cleanup
docker-compose -f docker-compose.gen.yml down
```

---

### 4.c Non-Privileged Testing with UCX

NIXL uses UCX (Unified Communication X) as a transport layer. UCX supports multiple transports, some of which do not require privileged access.

#### UCX Transport Options

| Transport | Env Var | Privileges | Performance | Use Case |
|-----------|---------|------------|-------------|----------|
| `tcp` | `UCX_TLS=tcp` | None | Moderate (~10 Gbps) | Cross-host testing, CI/CD |
| `shm` | `UCX_TLS=shm` | None | Good (~50+ GB/s) | Single-host testing |
| `rc` (RDMA RC) | `UCX_TLS=rc` | Privileged | Highest | Production |
| `dc` (RDMA DC) | `UCX_TLS=dc` | Privileged | Highest | Production |

#### Running Non-Privileged Tests

**Environment variable configuration:**

```bash
# Force UCX to use only TCP transport (no RDMA, no privileges needed)
export UCX_TLS=tcp

# For single-host testing, use shared memory for better performance
export UCX_TLS=shm

# Combine for flexibility (use shm when possible, fall back to tcp)
export UCX_TLS=shm,tcp

# Disable RDMA transports explicitly
export UCX_TLS=tcp,shm
export UCX_NET_DEVICES=all  # Use all network devices
```

**Docker Compose for non-privileged testing:**

```yaml
# docker-compose.unprivileged.yml
version: '3.8'

services:
  controller:
    build:
      context: .
      dockerfile: Dockerfile.controller
    container_name: topo-controller
    # NO privileged: true
    # NO network_mode: host
    networks:
      - topo-net
    environment:
      - UCX_TLS=tcp,shm           # Force non-RDMA transports
      - UCX_LOG_LEVEL=warn
      - CONTROLLER_PORT=50000
      - NUM_AGENTS=4
    ports:
      - "50000:50000"
    command: ["--port", "50000", "--agents", "4"]

  agent-0:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-0
    networks:
      - topo-net
    environment:
      - UCX_TLS=tcp,shm
      - UCX_LOG_LEVEL=warn
      - CONTROLLER_HOST=controller
      - CONTROLLER_PORT=50000
    depends_on:
      - controller
    command: ["--controller", "controller:50000"]

  agent-1:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-1
    networks:
      - topo-net
    environment:
      - UCX_TLS=tcp,shm
    depends_on:
      - controller
    command: ["--controller", "controller:50000"]

networks:
  topo-net:
    driver: bridge
```

**Running non-privileged tests:**

```bash
# Start without any special permissions
docker-compose -f docker-compose.unprivileged.yml up -d

# Verify UCX is using TCP/SHM
docker exec topo-agent-0 ucx_info -d  # Shows available transports

# Run tests - NIXL works via UCX TCP, no RDMA required
docker exec topo-controller topo-ctl run-test --type PAIRWISE_LATENCY

# Results are valid for functional testing
# (Performance numbers won't match real RDMA)
docker exec topo-controller topo-ctl get-results
```

#### Unit Tests (No UCX/NIXL Required)

Pure algorithm tests run without any network stack:

```bash
# Build and run unit tests (no special permissions)
cmake -B build
cmake --build build --target unit_tests

./build/tests/unit_tests --gtest_filter="Clustering*"
./build/tests/unit_tests --gtest_filter="GraphConstruction*"
./build/tests/unit_tests --gtest_filter="Statistics*"
```

#### Testing Mode Summary

| Mode | UCX_TLS | Privileges | Containers | Use Case |
|------|---------|------------|------------|----------|
| Unit tests | N/A | None | N/A | Algorithm validation |
| UCX TCP | `tcp` | None | Standard | CI/CD, integration tests |
| UCX SHM | `shm` | None | Standard | Single-host functional tests |
| UCX RDMA | `rc,dc` | Privileged | Privileged + hostNetwork | Production measurements |

**Important Notes:**
- Performance measurements from TCP/SHM modes are not representative of real RDMA performance
- Use TCP/SHM modes for functional correctness testing only
- Topology detection algorithms will work but latency thresholds may need adjustment for simulated transports
- For accurate performance characterization, full RDMA mode is required

---

### 4.d Test Harness Summary

| Phase | Feature | Purpose |
|-------|---------|---------|
| **Phase 1** | Basic containers (RDMA) | Production, real measurements |
| **Phase 1** | UCX TCP/SHM mode | Non-privileged functional testing |
| **Phase 1** | Unit tests | Algorithm validation |
| **Phase 2** | Custom networks + TC | Simulate multi-rack topologies |
| **Phase 2** | Scenario runner | Automated topology validation |

#### Phase 1 Deliverables
- `Dockerfile.agent`, `Dockerfile.controller`
- `docker-compose.yml` for single-host deployment
- Basic Kubernetes manifests

#### Phase 2 Deliverables
- `docker-compose.topology.yml` with custom networks
- Traffic control scripts (`setup-tc.sh`)
- Scenario definitions (YAML)
- Topology comparison tools

---

## 5. Error Handling & Recovery

> **Implementation Note:** This section describes **Phase 2 features**. Implement basic functionality first, then add error handling and recovery mechanisms.

### 5.a Failure Modes

| Failure | Detection | Impact | Recovery |
|---------|-----------|--------|----------|
| Agent crash mid-test | Heartbeat timeout | Test pair incomplete | Skip pair, continue with remaining |
| Agent crash during rendezvous | TCP disconnect / slot not populated | Rendezvous hangs | Timeout, abort or continue with N-1 |
| Controller crash | Agents poll indefinitely | All state lost | Agents must restart (no recovery) |
| NIXL transfer failure | `nixl_wait()` returns error | Single operation fails | Retry with backoff, then mark pair failed |
| Network partition | Heartbeat timeout | Subset of agents unreachable | Graceful degradation, test reachable pairs |

---

### 5.b Agent Heartbeat & Health Monitoring

**Extended agent status with heartbeat:**

```cpp
struct AgentStatus {
    uint64_t status_seq;
    AgentState state;
    uint64_t last_ack_seq;
    uint32_t error_code;

    // Phase 2: Health monitoring
    uint64_t heartbeat_ts;        // Timestamp of last heartbeat (ns since epoch)
    uint32_t consecutive_errors;  // Incremented on failures, reset on success
};

// Constants
constexpr uint64_t HEARTBEAT_INTERVAL_MS = 5000;   // Agent updates every 5s
constexpr uint64_t AGENT_TIMEOUT_MS = 30000;       // Dead after 30s silence
constexpr uint32_t MAX_CONSECUTIVE_ERRORS = 3;    // Mark unhealthy after 3 failures
```

**Agent heartbeat loop (background thread):**

```cpp
void Agent::heartbeat_loop() {
    while (running_) {
        auto* status = get_my_status_slot();
        status->heartbeat_ts = now_ns();
        nixl_flush();  // Ensure write visible to controller

        std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
    }
}
```

**Controller health check:**

```cpp
enum class AgentHealth { HEALTHY, SLOW, DEAD };

AgentHealth Controller::check_agent_health(uint32_t agent_id) {
    auto* status = buffer_->get_agent_status(agent_id);
    uint64_t now = now_ns();
    uint64_t age_ms = (now - status->heartbeat_ts) / 1'000'000;

    if (age_ms > AGENT_TIMEOUT_MS) {
        return AgentHealth::DEAD;
    } else if (age_ms > HEARTBEAT_INTERVAL_MS * 3) {
        return AgentHealth::SLOW;
    }
    return AgentHealth::HEALTHY;
}

vector<uint32_t> Controller::get_live_agents() {
    vector<uint32_t> live;
    for (uint32_t i = 0; i < num_agents_; i++) {
        if (check_agent_health(i) != AgentHealth::DEAD) {
            live.push_back(i);
        }
    }
    return live;
}
```

---

### 5.c Test Execution Timeouts

**Per-operation timeouts:**

```cpp
struct TimeoutConfig {
    uint64_t rendezvous_timeout_ms = 60000;      // 1 min for all agents to join
    uint64_t configure_timeout_ms = 10000;       // 10s for agents to become READY
    uint64_t test_pair_timeout_ms = 300000;      // 5 min per pair (worst case)
    uint64_t collect_timeout_ms = 30000;         // 30s to upload results
};
```

**Timeout handling in test manager:**

```cpp
enum class WaitResult { SUCCESS, TIMEOUT, AGENT_DEAD };

WaitResult TestManager::wait_for_agents_with_timeout(
    const vector<uint32_t>& agent_ids,
    AgentState expected,
    uint64_t timeout_ms
) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_ready = true;
        for (uint32_t id : agent_ids) {
            // Check if agent died
            if (check_agent_health(id) == AgentHealth::DEAD) {
                LOG_ERROR("Agent {} died while waiting for state {}", id, expected);
                return WaitResult::AGENT_DEAD;
            }

            auto* status = buffer_->get_agent_status(id);
            if (status->state != expected) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) return WaitResult::SUCCESS;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    LOG_WARN("Timeout waiting for agents to reach state {}", expected);
    return WaitResult::TIMEOUT;
}
```

---

### 5.d Graceful Degradation

**Policy for handling failures during test execution:**

```cpp
enum class FailurePolicy {
    ABORT,              // Stop entire test on any failure
    SKIP_AND_CONTINUE,  // Skip failed pair, continue with others
    RETRY_THEN_SKIP     // Retry N times, then skip
};

struct TestExecutionConfig {
    FailurePolicy failure_policy = FailurePolicy::SKIP_AND_CONTINUE;
    uint32_t max_retries = 2;
    bool allow_partial_results = true;  // Accept results even if some pairs failed
};
```

**Test manager with graceful degradation:**

```cpp
void TestManager::run_pairwise_test_resilient(TestType type) {
    auto pairs = generate_all_pairs();
    vector<pair<uint32_t, uint32_t>> failed_pairs;
    vector<pair<uint32_t, uint32_t>> skipped_pairs;

    for (const auto& pair : pairs) {
        // Skip if either agent is dead
        if (check_agent_health(pair.first) == AgentHealth::DEAD ||
            check_agent_health(pair.second) == AgentHealth::DEAD) {
            skipped_pairs.push_back(pair);
            LOG_WARN("Skipping pair ({}, {}): agent unavailable",
                     pair.first, pair.second);
            continue;
        }

        uint32_t attempts = 0;
        bool success = false;

        while (attempts <= config_.max_retries && !success) {
            auto result = execute_single_pair(pair);
            if (result == WaitResult::SUCCESS) {
                success = true;
            } else {
                attempts++;
                LOG_WARN("Pair ({}, {}) failed, attempt {}/{}",
                         pair.first, pair.second, attempts, config_.max_retries);
            }
        }

        if (!success) {
            failed_pairs.push_back(pair);
            if (config_.failure_policy == FailurePolicy::ABORT) {
                throw TestAbortedException("Pair failed after retries");
            }
        }
    }

    // Report summary
    LOG_INFO("Test complete: {} succeeded, {} failed, {} skipped",
             pairs.size() - failed_pairs.size() - skipped_pairs.size(),
             failed_pairs.size(),
             skipped_pairs.size());

    // Store failure info in results
    results_.failed_pairs = failed_pairs;
    results_.skipped_pairs = skipped_pairs;
    results_.is_partial = !failed_pairs.empty() || !skipped_pairs.empty();
}
```

---

### 5.e NIXL Transfer Error Handling

**Wrapper with retry logic:**

```cpp
enum class NixlResult { SUCCESS, TRANSIENT_ERROR, PERMANENT_ERROR };

NixlResult nixl_write_with_retry(
    NixlEndpoint peer,
    NixlBufferDescriptor peer_buf,
    size_t local_offset,
    size_t remote_offset,
    size_t size,
    uint32_t max_retries = 3
) {
    for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
        auto handle = nixl_write_async(peer, peer_buf, local_offset, remote_offset, size);
        auto status = nixl_wait(handle, NIXL_TIMEOUT_MS);

        switch (status) {
            case NIXL_SUCCESS:
                return NixlResult::SUCCESS;

            case NIXL_ERR_TIMEOUT:
            case NIXL_ERR_RETRY:
                LOG_WARN("NIXL transient error, retry {}/{}", attempt + 1, max_retries);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10 * (1 << attempt))  // Exponential backoff
                );
                continue;

            case NIXL_ERR_DISCONNECTED:
            case NIXL_ERR_INVALID:
            default:
                LOG_ERROR("NIXL permanent error: {}", nixl_strerror(status));
                return NixlResult::PERMANENT_ERROR;
        }
    }
    return NixlResult::TRANSIENT_ERROR;
}
```

---

### 5.f Graceful Shutdown Protocol

**New command type:**

```cpp
enum class CommandType : uint32_t {
    IDLE = 0,
    CONFIGURE,
    START,
    STOP,
    COLLECT,
    FETCH_FULL_STATS,
    SHUTDOWN           // Phase 2: Clean shutdown
};
```

**Agent shutdown handling:**

```cpp
void Agent::test_loop() {
    while (running_) {
        TestCommand cmd = nixl_read_test_command(controller_buffer_);

        // ... existing command handling ...

        case CommandType::SHUTDOWN:
            LOG_INFO("Received SHUTDOWN command, cleaning up");
            cleanup();
            running_ = false;
            break;
    }
}

void Agent::cleanup() {
    // Deregister NIXL buffer
    nixl_deregister_buffer(test_buffer_desc_);

    // Free memory
    free_registered_memory(test_buffer_, test_buffer_size_);

    // Close NIXL endpoint
    nixl_destroy_endpoint(endpoint_);

    LOG_INFO("Agent {} shutdown complete", my_id_);
}
```

**Controller shutdown sequence:**

```cpp
void Controller::shutdown_gracefully() {
    // 1. Send SHUTDOWN to all agents
    auto* cmd = buffer_->get_test_command();
    cmd->command_seq++;
    cmd->command_type = CommandType::SHUTDOWN;

    // 2. Wait for agents to acknowledge (with timeout)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        bool all_idle = true;
        for (uint32_t i = 0; i < num_agents_; i++) {
            auto* status = buffer_->get_agent_status(i);
            if (status->state != AgentState::IDLE &&
                check_agent_health(i) != AgentHealth::DEAD) {
                all_idle = false;
                break;
            }
        }
        if (all_idle) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 3. Cleanup controller resources
    nixl_deregister_buffer(buffer_desc_);
    free_registered_memory(buffer_, buffer_size_);
    nixl_destroy_endpoint(endpoint_);

    LOG_INFO("Controller shutdown complete");
}
```

---

### 5.g Error Codes

```cpp
enum class ErrorCode : uint32_t {
    OK = 0,

    // Agent errors (1xx)
    ERR_BUFFER_ALLOC_FAILED = 100,
    ERR_NIXL_INIT_FAILED = 101,
    ERR_CONTROLLER_UNREACHABLE = 102,
    ERR_PEER_UNREACHABLE = 103,
    ERR_TEST_CONFIG_INVALID = 104,

    // Transfer errors (2xx)
    ERR_NIXL_WRITE_FAILED = 200,
    ERR_NIXL_READ_FAILED = 201,
    ERR_NIXL_TIMEOUT = 202,

    // Test errors (3xx)
    ERR_TEST_TIMEOUT = 300,
    ERR_PEER_NOT_READY = 301,
    ERR_BUFFER_TOO_SMALL = 302,

    // Controller errors (4xx)
    ERR_RENDEZVOUS_TIMEOUT = 400,
    ERR_AGENT_DIED = 401,
    ERR_RESULTS_INCOMPLETE = 402,
};

// Agent writes error_code to its status slot on failure
// Results include error_code for failed pairs
```

---

### 5.h Phase 2 Deliverables Summary

| Feature | Description | Priority |
|---------|-------------|----------|
| Agent heartbeat | Background thread updates `heartbeat_ts` | High |
| Health monitoring | Controller tracks live/dead agents | High |
| Test timeouts | Configurable per-operation timeouts | High |
| Graceful degradation | Skip failed pairs, continue testing | High |
| NIXL retry wrapper | Exponential backoff for transient errors | Medium |
| SHUTDOWN command | Clean resource deallocation | Medium |
| Error codes | Structured error reporting | Medium |

---

## 6. Optional Future Work

> **Note:** These features are not required for initial implementation. Consider adding them based on deployment requirements and operational experience.

### 6.a Security

**Current assumption:** Trusted network environment. All agents and controller run in a secured cluster with no malicious actors.

**Future enhancements if needed:**

#### Authentication

```cpp
// Shared secret authentication during TCP handshake
struct HandshakeRequest {
    uint32_t protocol_version;
    uint8_t auth_token[32];       // HMAC-SHA256 of shared secret + timestamp
    uint64_t timestamp_ns;
    uint8_t agent_nonce[16];
};

struct HandshakeResponse {
    uint32_t agent_id;
    uint8_t auth_response[32];    // HMAC of request + controller secret
    // ... existing blob fields ...
};

// Controller validates:
// 1. Timestamp within acceptable window (prevent replay)
// 2. auth_token matches expected HMAC
```

#### Data Integrity

```cpp
// Optional CRC32 on buffer writes
struct AgentMetadataSlot {
    uint64_t populated_flag;
    uint8_t nixl_endpoint_blob[ENDPOINT_BLOB_SIZE];
    uint8_t buffer_blob[BUFFER_BLOB_SIZE];
    uint32_t crc32;               // CRC of above fields
};

// Controller validates CRC before trusting agent metadata
```

#### Access Control

```cpp
// Agent capability flags (controller assigns during handshake)
enum AgentCapability : uint32_t {
    CAP_RUN_TESTS = 1 << 0,       // Can participate in tests
    CAP_UPLOAD_RESULTS = 1 << 1,  // Can write to results region
    CAP_READ_PEERS = 1 << 2,      // Can read peer metadata
};

// Limit damage from compromised agent
```

---

### 6.b Observability & Monitoring

#### Structured Logging

```cpp
// Log levels and structured format
enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, FATAL };

struct LogEntry {
    uint64_t timestamp_ns;
    LogLevel level;
    string component;      // "agent", "controller", "nixl", "test"
    string message;
    map<string, string> fields;  // Structured key-value pairs
};

// Example usage:
LOG_INFO("test", "Pair test complete",
         {{"src", "0"}, {"dst", "1"}, {"latency_ns", "1234"}, {"status", "ok"}});

// Output formats: console, JSON lines, syslog
```

#### Progress Reporting

```cpp
// Dedicated progress region in controller buffer
struct TestProgress {
    uint64_t test_start_ts;
    uint64_t last_update_ts;

    // Current status
    TestType current_test;
    uint32_t total_pairs;
    uint32_t completed_pairs;
    uint32_t failed_pairs;
    uint32_t skipped_pairs;

    // Current pair being tested
    uint32_t current_src;
    uint32_t current_dst;
    uint32_t current_iteration;
    uint32_t total_iterations;

    // ETA (estimated)
    uint64_t estimated_completion_ts;
};

// Controller updates this region periodically
// External monitoring can read via NIXL or REST endpoint
```

#### Metrics Export

```cpp
// Prometheus-style metrics
struct Metrics {
    // Counters
    uint64_t tests_completed_total;
    uint64_t tests_failed_total;
    uint64_t nixl_transfers_total;
    uint64_t nixl_transfer_bytes_total;

    // Gauges
    uint32_t agents_connected;
    uint32_t agents_healthy;
    double current_test_progress;  // 0.0 - 1.0

    // Histograms (optional)
    // latency_histogram, bandwidth_histogram
};

// Export via:
// - HTTP /metrics endpoint (Prometheus scrape)
// - StatsD push
// - Console summary on completion
```

#### Health Endpoint

```cpp
// Simple HTTP health check for orchestration systems
// GET /health returns:
struct HealthResponse {
    string status;              // "healthy", "degraded", "unhealthy"
    uint32_t agents_expected;
    uint32_t agents_connected;
    uint32_t agents_healthy;
    string current_test;        // "PAIRWISE_LATENCY" or "idle"
    double progress;            // 0.0 - 1.0
};
```

---

### 6.c Protocol Versioning

**Problem:** Protocol changes break compatibility between controller and agent versions.

**Solution:** Version negotiation during handshake.

```cpp
// Protocol version in buffer header
struct BufferHeader {
    uint32_t magic;              // 0x544F504F ("TOPO")
    uint32_t protocol_version;   // Major.Minor as (major << 16) | minor
    uint32_t num_agents;
    uint32_t agent_slots_offset;
    uint32_t test_ctrl_offset;
    uint32_t results_offset;
    uint64_t ready_flag;
};

constexpr uint32_t PROTOCOL_VERSION_1_0 = (1 << 16) | 0;
constexpr uint32_t PROTOCOL_VERSION_1_1 = (1 << 16) | 1;
constexpr uint32_t CURRENT_PROTOCOL_VERSION = PROTOCOL_VERSION_1_0;

// Compatibility rules:
// - Same major version: compatible (minor adds optional features)
// - Different major version: incompatible, abort with error
```

**Version negotiation:**

```cpp
// During TCP handshake
struct HandshakeRequest {
    uint32_t min_protocol_version;  // Oldest version agent supports
    uint32_t max_protocol_version;  // Newest version agent supports
    // ...
};

struct HandshakeResponse {
    uint32_t negotiated_version;    // Controller picks within range
    // ...
};

// Agent validates negotiated_version is acceptable
```

---

### 6.d Result Validation

**Problem:** Bad data (clock issues, hardware faults, bugs) can produce incorrect topology.

**Solution:** Sanity checks before graph construction.

```cpp
struct ValidationConfig {
    // Latency bounds
    uint64_t min_valid_latency_ns = 100;          // < 100ns suspicious
    uint64_t max_valid_latency_ns = 1'000'000'000; // > 1s suspicious

    // Bandwidth bounds (GB/s)
    double min_valid_bandwidth = 0.001;   // < 1 MB/s suspicious
    double max_valid_bandwidth = 1000.0;  // > 1 TB/s suspicious

    // Symmetry tolerance
    double max_asymmetry_ratio = 2.0;     // A→B vs B→A within 2x

    // Statistical outliers
    double outlier_stddev_threshold = 4.0; // Flag > 4 sigma outliers
};

struct ValidationResult {
    bool valid;
    vector<string> warnings;
    vector<string> errors;

    // Specific issues found
    vector<pair<uint32_t, uint32_t>> suspicious_latencies;
    vector<pair<uint32_t, uint32_t>> suspicious_bandwidths;
    vector<pair<uint32_t, uint32_t>> asymmetric_pairs;
};

ValidationResult validate_results(
    const LatencyMatrix& L,
    const BandwidthMatrix& B,
    const ValidationConfig& config
) {
    ValidationResult result;

    // 1. Check latency bounds
    for (uint32_t i = 0; i < L.num_agents; i++) {
        for (uint32_t j = 0; j < L.num_agents; j++) {
            if (i == j) continue;
            auto lat = L.latency[i][j].avg_ns;
            if (lat < config.min_valid_latency_ns ||
                lat > config.max_valid_latency_ns) {
                result.warnings.push_back(
                    fmt::format("Suspicious latency {}->{}: {} ns", i, j, lat));
                result.suspicious_latencies.push_back({i, j});
            }
        }
    }

    // 2. Check symmetry
    for (uint32_t i = 0; i < L.num_agents; i++) {
        for (uint32_t j = i + 1; j < L.num_agents; j++) {
            double ratio = (double)L.latency[i][j].avg_ns /
                          (double)L.latency[j][i].avg_ns;
            if (ratio > config.max_asymmetry_ratio ||
                ratio < 1.0 / config.max_asymmetry_ratio) {
                result.warnings.push_back(
                    fmt::format("Asymmetric latency {}<->{}: ratio {:.2f}", i, j, ratio));
                result.asymmetric_pairs.push_back({i, j});
            }
        }
    }

    // 3. Statistical outlier detection
    // ... compute mean/stddev, flag outliers ...

    result.valid = result.errors.empty();
    return result;
}
```

**Integration with graph construction:**

```cpp
TopologyGraph build_topology(const LatencyMatrix& L, const BandwidthMatrix& B) {
    // Validate first
    auto validation = validate_results(L, B, default_validation_config);

    if (!validation.valid) {
        LOG_ERROR("Result validation failed:");
        for (const auto& err : validation.errors) {
            LOG_ERROR("  {}", err);
        }
        throw InvalidResultsException(validation);
    }

    if (!validation.warnings.empty()) {
        LOG_WARN("Result validation warnings ({}):", validation.warnings.size());
        for (const auto& warn : validation.warnings) {
            LOG_WARN("  {}", warn);
        }
        // Continue with warnings, but flag in output
    }

    // Proceed with graph construction...
}
```

---

### 6.e Future Work Summary

| Feature | Description | When to Consider |
|---------|-------------|------------------|
| **Security** | Auth, integrity checks, access control | Multi-tenant or untrusted environments |
| **Observability** | Structured logging, metrics, progress | Production deployments |
| **Protocol versioning** | Version negotiation, compatibility | Before first breaking change |
| **Result validation** | Sanity checks, outlier detection | When seeing incorrect topologies |

---

## Module Structure

```
nixl-topo-disc/
├── src/
│   ├── agent/
│   │   ├── agent_main.cpp
│   │   ├── bootstrap.cpp
│   │   ├── nixl_comm.cpp
│   │   └── perf_tests.cpp
│   ├── controller/
│   │   ├── controller_main.cpp
│   │   ├── rendezvous.cpp
│   │   ├── test_manager.cpp
│   │   └── data_collector.cpp
│   ├── topology/
│   │   └── [placeholder]
│   ├── harness/
│   │   └── [placeholder]
│   └── common/
│       ├── nixl_wrapper.hpp
│       ├── protocol.hpp
│       └── types.hpp
├── include/
├── tests/
├── CLAUDE.md
└── CMakeLists.txt
```

---

## Build & Dependencies

- **Language:** C++17 or later
- **Build System:** CMake
- **Dependencies:**
  - NVIDIA NIXL SDK
  - POSIX sockets (for minimal TCP bootstrap handshake only)
  - (Optional) nlohmann/json for serialization
