// =============================================================================
// network_manager.hpp
// Docker network management for test harness
// =============================================================================

#ifndef HARNESS_NETWORK_MANAGER_HPP
#define HARNESS_NETWORK_MANAGER_HPP

#include <string>
#include <vector>

namespace harness {

/**
 * Manages Docker network creation and teardown
 */
class NetworkManager {
public:
    /**
     * Create a Docker network with the specified configuration
     * @param name Network name
     * @param subnet Network subnet (e.g., "172.30.0.0/16")
     * @return true if network created successfully
     */
    static bool create_network(const std::string& name, const std::string& subnet);

    /**
     * Remove a Docker network
     * @param name Network name to remove
     * @return true if network removed successfully
     */
    static bool remove_network(const std::string& name);

    /**
     * Check if a Docker network exists
     * @param name Network name to check
     * @return true if network exists
     */
    static bool network_exists(const std::string& name);

    /**
     * List all containers on a network
     * @param network_name Network name
     * @return Vector of container names
     */
    static std::vector<std::string> list_containers_on_network(const std::string& network_name);

    /**
     * Stop and remove a container
     * @param container_name Container name
     * @return true if container stopped and removed
     */
    static bool remove_container(const std::string& container_name);

    /**
     * Stop and remove all containers on a network
     * @param network_name Network name
     * @return Number of containers removed
     */
    static int remove_all_containers(const std::string& network_name);

private:
    /**
     * Execute a command and return its output
     * @param cmd Command to execute
     * @param output Output string (filled on success)
     * @return Exit code
     */
    static int exec_command(const std::string& cmd, std::string& output);

    /**
     * Execute a command (output discarded)
     * @param cmd Command to execute
     * @return Exit code
     */
    static int exec_command(const std::string& cmd);
};

}  // namespace harness

#endif  // HARNESS_NETWORK_MANAGER_HPP
