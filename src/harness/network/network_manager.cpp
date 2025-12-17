// =============================================================================
// network_manager.cpp
// Docker network management implementation
// =============================================================================

#include "network_manager.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <iostream>

namespace harness {

int NetworkManager::exec_command(const std::string& cmd, std::string& output) {
    std::array<char, 128> buffer;
    output.clear();

    std::string full_cmd = cmd + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);

    if (!pipe) {
        return -1;
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }

    return pclose(pipe.release());
}

int NetworkManager::exec_command(const std::string& cmd) {
    std::string output;
    return exec_command(cmd, output);
}

bool NetworkManager::create_network(const std::string& name, const std::string& subnet) {
    // Check if network already exists
    if (network_exists(name)) {
        std::cerr << "Network '" << name << "' already exists\n";
        return true;  // Not an error - network is usable
    }

    std::ostringstream cmd;
    cmd << "docker network create --subnet=" << subnet << " " << name;

    std::string output;
    int rc = exec_command(cmd.str(), output);

    if (rc != 0) {
        std::cerr << "Failed to create network: " << output << "\n";
        return false;
    }

    std::cout << "Created network: " << name << " (" << subnet << ")\n";
    return true;
}

bool NetworkManager::remove_network(const std::string& name) {
    if (!network_exists(name)) {
        return true;  // Already gone
    }

    std::string cmd = "docker network rm " + name;
    std::string output;
    int rc = exec_command(cmd, output);

    if (rc != 0) {
        std::cerr << "Failed to remove network: " << output << "\n";
        return false;
    }

    std::cout << "Removed network: " << name << "\n";
    return true;
}

bool NetworkManager::network_exists(const std::string& name) {
    std::string cmd = "docker network inspect " + name + " >/dev/null 2>&1";
    return exec_command(cmd) == 0;
}

std::vector<std::string> NetworkManager::list_containers_on_network(const std::string& network_name) {
    std::vector<std::string> containers;

    std::string cmd = "docker network inspect " + network_name +
                      " --format '{{range .Containers}}{{.Name}} {{end}}' 2>/dev/null";

    std::string output;
    int rc = exec_command(cmd, output);

    if (rc != 0 || output.empty()) {
        return containers;
    }

    // Parse space-separated container names
    std::istringstream iss(output);
    std::string container;
    while (iss >> container) {
        if (!container.empty()) {
            containers.push_back(container);
        }
    }

    return containers;
}

bool NetworkManager::remove_container(const std::string& container_name) {
    // Stop container (ignore errors if already stopped)
    std::string stop_cmd = "docker stop " + container_name + " 2>/dev/null";
    exec_command(stop_cmd);

    // Remove container
    std::string rm_cmd = "docker rm -f " + container_name + " 2>/dev/null";
    int rc = exec_command(rm_cmd);

    if (rc == 0) {
        std::cout << "Removed container: " << container_name << "\n";
    }

    return rc == 0;
}

int NetworkManager::remove_all_containers(const std::string& network_name) {
    auto containers = list_containers_on_network(network_name);
    int removed = 0;

    for (const auto& container : containers) {
        if (remove_container(container)) {
            removed++;
        }
    }

    return removed;
}

}  // namespace harness
