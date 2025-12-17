// =============================================================================
// harness.hpp
// Test harness for containerized cluster deployment
// =============================================================================

#ifndef HARNESS_HARNESS_HPP
#define HARNESS_HARNESS_HPP

#include "config/cluster_config.hpp"
#include <string>

namespace harness {

/**
 * Test harness for managing containerized cluster
 */
class Harness {
public:
    /**
     * Construct harness with cluster configuration
     * @param config Cluster configuration
     */
    explicit Harness(const ClusterConfig& config);

    /**
     * Bring up the cluster (create network + start controller)
     * @param output_dir Directory for output files (mounted into container)
     * @param test_config Optional test config file (relative to output_dir)
     * @return true if cluster started successfully
     */
    bool up(const std::string& output_dir = "./output",
            const std::string& test_config = "");

    /**
     * Wait for tests to complete and collect results
     * Creates timestamped subdirectory with results and topology SVG
     * @param output_dir Base output directory
     * @return Path to results subdirectory, or empty string on failure
     */
    std::string collect_results(const std::string& output_dir = "./output");

    /**
     * Bring down the cluster (stop containers + remove network)
     * @return true if cluster stopped successfully
     */
    bool down();

    /**
     * Show cluster status
     */
    void status() const;

private:
    ClusterConfig config_;

    /**
     * Start the controller container
     * @param output_dir Output directory to mount
     * @param test_config Optional test config file path (inside container)
     * @return true if controller started
     */
    bool start_controller(const std::string& output_dir,
                          const std::string& test_config = "");

    /**
     * Wait for controller to become healthy
     * @param timeout_sec Timeout in seconds
     * @return true if controller is healthy
     */
    bool wait_for_controller(int timeout_sec = 30);

    /**
     * Execute a Docker command
     * @param cmd Command to execute
     * @param output Output string (filled on success)
     * @return Exit code
     */
    static int exec_command(const std::string& cmd, std::string& output);

    /**
     * Check if a container is running
     * @param container_name Container name
     * @return true if running
     */
    static bool container_running(const std::string& container_name);
};

}  // namespace harness

#endif  // HARNESS_HARNESS_HPP
