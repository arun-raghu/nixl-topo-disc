
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
| **Container orchestrator for bootstrap** | Requires orchestrator (Docker/K8s) but eliminates TCP code. Controller passes NIXL metadata via env vars. |
