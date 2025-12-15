
---

## 6. Optional Future Work

> **Note:** These features are not required for initial implementation. Consider adding them based on deployment requirements and operational experience.

### 6.a Security

**Current assumption:** Trusted network environment. All agents and controller run in a secured cluster with no malicious actors.

**Future enhancements if needed:**

#### Authentication

```cpp
// Shared secret authentication via environment variable
// Controller generates unique token per agent, passes via env var

// Agent receives auth token at container startup
struct AgentAuthConfig {
    uint8_t auth_token[32];       // HMAC-SHA256 of shared secret + agent_id
    uint64_t token_expiry_ns;     // Token validity window
};

// Agent includes auth token when writing to controller buffer
struct AuthenticatedMetadataSlot {
    uint64_t populated_flag;
    uint8_t auth_token[32];       // Must match expected token for this agent_id
    uint8_t nixl_endpoint_blob[ENDPOINT_BLOB_SIZE];
    uint8_t buffer_blob[BUFFER_BLOB_SIZE];
};

// Controller validates:
// 1. auth_token matches expected HMAC for agent_id
// 2. Token not expired
// 3. Slot write came from expected agent (prevents slot hijacking)
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
// Agent capability flags (controller assigns at container spawn via env var)
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
// Controller passes protocol version via environment variable
// Agent validates compatibility at startup

// Environment variable set by controller:
//   PROTOCOL_VERSION=65536  (1.0 as uint32)

void Agent::validate_protocol_version() {
    uint32_t controller_version = std::stoul(std::getenv("PROTOCOL_VERSION"));
    uint32_t controller_major = controller_version >> 16;
    uint32_t agent_major = CURRENT_PROTOCOL_VERSION >> 16;

    if (controller_major != agent_major) {
        LOG_ERROR("Protocol version mismatch: controller={}.{}, agent={}.{}",
                  controller_major, controller_version & 0xFFFF,
                  agent_major, CURRENT_PROTOCOL_VERSION & 0xFFFF);
        throw std::runtime_error("Incompatible protocol version");
    }

    LOG_INFO("Protocol version {} validated", controller_version);
}
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