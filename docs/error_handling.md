
---

## 5. Error Handling & Recovery

> **Implementation Note:** This section describes **Phase 2 features**. Implement basic functionality first, then add error handling and recovery mechanisms.

### 5.a Failure Modes

| Failure | Detection | Impact | Recovery |
|---------|-----------|--------|----------|
| Agent crash mid-test | Heartbeat timeout | Test pair incomplete | Skip pair, continue with remaining |
| Agent crash during rendezvous | Slot not populated / container exit | Rendezvous hangs | Timeout, abort or continue with N-1 |
| Controller crash | Agents wait on notifications indefinitely | All state lost | Agents must restart (no recovery) |
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