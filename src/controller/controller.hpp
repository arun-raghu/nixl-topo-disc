#pragma once

#include "controller_buffer.hpp"
#include "../common/nixl_wrapper.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <chrono>

namespace nixl_topo {

/// Stores the result of a single ping-pong latency test
struct LatencyResult {
    uint32_t initiator_id;
    uint32_t responder_id;
    uint64_t avg_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    bool success;
};

/// Stores the result of a single bandwidth test
struct BandwidthResult {
    uint32_t sender_id;
    uint32_t receiver_id;
    uint64_t message_size;
    uint64_t bandwidth_mbps;
    uint64_t total_bytes;
    uint64_t elapsed_ns;
    bool success;
};

/// Stores detailed latency result per message size (for latency sweep)
struct LatencyDetailedResult {
    uint32_t initiator_id;
    uint32_t responder_id;
    uint64_t message_size;
    uint64_t avg_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    bool success;
};

/// Configuration for tests loaded from JSON
struct TestConfig {
    // Ping-pong latency test parameters
    uint64_t message_size = 64;           // Payload size in bytes
    uint32_t iterations = 10;             // Number of measured iterations
    uint32_t warmup_iterations = 5;       // Number of warmup iterations
    uint32_t result_timeout_sec = 30;     // Timeout waiting for results

    // Bandwidth test parameters
    std::vector<uint64_t> bw_message_sizes = {1024, 4096, 16384, 65536, 262144, 1048576, 4194304};  // 1K to 4M
    uint32_t bw_iterations = 100;         // Number of measured windows
    uint32_t bw_warmup_iterations = 10;   // Warmup windows
    uint32_t bw_window_size = 64;         // Outstanding messages per window

    // Latency sweep parameters (for transfer time vs message size curves)
    std::vector<uint64_t> latency_message_sizes = {8, 16, 32, 64, 128, 256, 512,
                                                    1024, 2048, 4096, 8192, 16384,
                                                    32768, 65536};  // 8B to 64KB
    uint32_t latency_sweep_iterations = 1000;    // Measured iterations per size
    uint32_t latency_sweep_warmup = 100;         // Warmup iterations per size
    bool run_latency_sweep = false;              // Disabled by default (takes longer)

    // Output configuration
    std::string output_csv_path = "/tmp/latency_matrix.csv";  // Default output path
    std::string bandwidth_csv_path = "/tmp/bandwidth_matrix.csv";  // Bandwidth peak matrix
    std::string bandwidth_detailed_csv_path = "/tmp/bandwidth_detailed.csv";  // All msg sizes
    std::string latency_detailed_csv_path = "/tmp/latency_detailed.csv";  // Latency sweep results

    // Test selection (empty = test all pairs)
    bool test_all_pairs = true;           // If true, test all agent pairs
    bool run_bandwidth_tests = true;      // If true, run bandwidth tests after latency

    /// Load config from JSON file
    /// @param filepath Path to JSON config file
    /// @return Loaded TestConfig (uses defaults for missing fields)
    /// @throws std::runtime_error if file cannot be opened or parsed
    static TestConfig from_json(const std::string& filepath);

    /// Log config to stdout
    void print() const;
};

/// Main controller class for agent bootstrap and coordination.
class Controller {
public:
    Controller();
    ~Controller();

    // Disable copy
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    /// Initialize the controller for the given number of agents.
    /// Sets up NIXL and allocates the shared buffer.
    /// @param num_agents Number of agents that will connect
    /// @return true on success
    bool initialize(uint32_t num_agents);

    /// Check if initialized.
    bool is_initialized() const { return initialized_; }

    /// Get number of agents.
    uint32_t num_agents() const { return num_agents_; }

    /// Get environment variables to pass when spawning an agent container.
    /// @param agent_id Agent ID (0 to num_agents-1)
    /// @return Map of environment variable name to value
    std::map<std::string, std::string> get_agent_env_vars(uint32_t agent_id) const;

    /// Wait for a specific agent to register (write its metadata).
    /// @param agent_id Agent ID to wait for
    /// @param timeout Maximum time to wait
    /// @return true if agent registered, false on timeout
    bool wait_for_agent(uint32_t agent_id, std::chrono::milliseconds timeout);

    /// Wait for all agents to register.
    /// @param timeout Maximum time to wait for all agents
    /// @return true if all agents registered, false on timeout
    bool wait_for_all_agents(std::chrono::milliseconds timeout);

    /// Signal all agents that rendezvous is complete.
    /// Writes RENDEZVOUS_COMPLETE notification to all agent notification slots.
    void signal_rendezvous_complete();

    /// Wait for all agents to complete peer discovery.
    /// @param timeout Maximum time to wait
    /// @return true if all agents reported peer discovery complete, false on timeout
    bool wait_for_peer_discovery(std::chrono::milliseconds timeout);

    /// Shutdown the controller and release resources.
    void shutdown();

    /// Get the controller buffer (for testing/debugging).
    const ControllerBuffer& buffer() const { return buffer_; }

    /// Issue a ping-pong latency test between two agents.
    /// Controller writes command to both agents' command inboxes via RDMA.
    /// @param initiator_id Agent that starts ping and measures RTT
    /// @param responder_id Agent that responds to pings
    /// @param message_size Payload size in bytes
    /// @param iterations Number of measured iterations
    /// @param warmup_iterations Number of warmup iterations (not measured)
    /// @return true if commands written successfully
    bool issue_ping_pong_test(uint32_t initiator_id, uint32_t responder_id,
                              uint64_t message_size, uint32_t iterations,
                              uint32_t warmup_iterations = 10);

    /// Issue a unidirectional bandwidth test between two agents.
    /// INITIATOR (sender) sends window_size messages, RESPONDER (receiver) ACKs.
    /// @param sender_id Agent that sends data (INITIATOR role)
    /// @param receiver_id Agent that receives data (RESPONDER role)
    /// @param message_size Payload size in bytes per message
    /// @param iterations Number of measured windows
    /// @param warmup_iterations Number of warmup windows
    /// @param window_size Number of outstanding messages per window
    /// @return true if commands written successfully
    bool issue_bandwidth_test(uint32_t sender_id, uint32_t receiver_id,
                              uint64_t message_size, uint32_t iterations,
                              uint32_t warmup_iterations, uint32_t window_size);

    /// Send SHUTDOWN command to all registered agents.
    /// Agents will exit their command loops upon receiving this.
    /// @return true if commands sent successfully to all agents
    bool shutdown_agents();

    /// Wait for test results from specified agents.
    /// @param agent_ids Agents to wait for
    /// @param expected_seq Expected command_seq in results
    /// @param timeout Maximum time to wait
    /// @return true if all agents reported results, false on timeout
    bool wait_for_results(const std::vector<uint32_t>& agent_ids,
                          uint64_t expected_seq,
                          std::chrono::milliseconds timeout);

    /// Get current command sequence number.
    uint64_t command_seq() const { return command_seq_; }

    /// Get test result for an agent (in wire format, call from_wire() to convert).
    /// @param agent_id Agent to get result for
    /// @return Pointer to result, or nullptr if not available
    const TestResult* get_result(uint32_t agent_id) const;

    /// Store a latency test result for later CSV output.
    /// @param initiator_id Agent that initiated the test
    /// @param responder_id Agent that responded
    /// @param result Test result from the initiator
    void store_test_result(uint32_t initiator_id, uint32_t responder_id, const TestResult& result);

    /// Store a bandwidth test result for later CSV output.
    /// @param sender_id Agent that sent data
    /// @param receiver_id Agent that received data
    /// @param message_size Message size used in test
    /// @param result Test result from the sender
    void store_bandwidth_result(uint32_t sender_id, uint32_t receiver_id,
                                 uint64_t message_size, const TestResult& result);

    /// Log the latency matrix in CSV format.
    /// Format: NxN matrix, row i col j = latency from node i to node j (ns)
    /// Diagonal is 0, unmeasured pairs are -1
    /// @param output Output stream (e.g., std::cout or file)
    void log_latency_matrix_csv(std::ostream& output) const;

    /// Log the bandwidth matrix in CSV format.
    /// Format: NxN matrix, row i col j = bandwidth from node i to node j (MB/s)
    /// Diagonal is 0, unmeasured pairs are -1
    /// @param output Output stream (e.g., std::cout or file)
    void log_bandwidth_matrix_csv(std::ostream& output) const;

    /// Log detailed bandwidth results (all message sizes) in CSV format.
    /// Format: sender,receiver,msg_size,bandwidth_mbps
    /// @param output Output stream (e.g., std::cout or file)
    void log_bandwidth_detailed_csv(std::ostream& output) const;

    /// Store a detailed latency result for a specific message size (for latency sweep).
    /// @param initiator_id Agent that initiated the test
    /// @param responder_id Agent that responded
    /// @param message_size Message size used in test
    /// @param result Test result from the initiator
    void store_latency_detailed_result(uint32_t initiator_id, uint32_t responder_id,
                                        uint64_t message_size, const TestResult& result);

    /// Log detailed latency results (all message sizes) in CSV format.
    /// Format: initiator,responder,msg_size,avg_latency_ns,min_latency_ns,max_latency_ns
    /// @param output Output stream (e.g., std::cout or file)
    void log_latency_detailed_csv(std::ostream& output) const;

    /// Get stored latency results.
    const std::vector<LatencyResult>& latency_results() const { return latency_results_; }

    /// Get stored bandwidth results.
    const std::vector<BandwidthResult>& bandwidth_results() const { return bandwidth_results_; }

    /// Get stored detailed latency results (for latency sweep).
    const std::vector<LatencyDetailedResult>& latency_detailed_results() const { return latency_detailed_results_; }

private:
    /// Load metadata for a newly registered agent so we can send it notifications.
    bool load_agent_metadata(uint32_t agent_id);

    ControllerBuffer buffer_;
    NixlWrapper nixl_;
    uint32_t num_agents_ = 0;
    bool initialized_ = false;
    uint64_t notification_seq_ = 0;
    uint64_t command_seq_ = 0;
    std::vector<std::string> loaded_agent_names_;  // Agent names loaded for notifications
    std::vector<bool> agent_metadata_loaded_;       // Track which agents have been loaded
    std::vector<LatencyResult> latency_results_;    // Stored latency results for CSV output
    std::vector<BandwidthResult> bandwidth_results_;  // Stored bandwidth results for CSV output
    std::vector<LatencyDetailedResult> latency_detailed_results_;  // Stored detailed latency results (sweep)
};

} // namespace nixl_topo
