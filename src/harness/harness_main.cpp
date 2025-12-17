// =============================================================================
// harness_main.cpp
// Test harness CLI entry point
// =============================================================================

#include "harness.hpp"
#include "config/cluster_config.hpp"

#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " --config <cluster.json> <command>\n\n";
    std::cout << "Commands:\n";
    std::cout << "  up       Create network and start cluster\n";
    std::cout << "  collect  Wait for completion and collect results to timestamped dir\n";
    std::cout << "  down     Stop cluster and remove network\n";
    std::cout << "  status   Show cluster status\n\n";
    std::cout << "Options:\n";
    std::cout << "  --config <file>       Path to cluster.json configuration\n";
    std::cout << "  --output <dir>        Output directory (default: ./output)\n";
    std::cout << "  --test-config <file>  Test config JSON (path inside container, e.g., /output/test.json)\n";
    std::cout << "  --help                Show this help message\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << program << " --config cluster.json up\n";
    std::cout << "  " << program << " --config cluster.json --test-config /output/test.json up\n";
    std::cout << "  " << program << " --config cluster.json status\n";
    std::cout << "  " << program << " --config cluster.json down\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;
    std::string output_dir = "./output";
    std::string test_config;
    std::string command;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--config") == 0 || std::strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                config_path = argv[++i];
            } else {
                std::cerr << "Error: --config requires an argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_dir = argv[++i];
            } else {
                std::cerr << "Error: --output requires an argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--test-config") == 0 || std::strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                test_config = argv[++i];
            } else {
                std::cerr << "Error: --test-config requires an argument\n";
                return 1;
            }
        } else if (argv[i][0] != '-') {
            // Positional argument - command
            command = argv[i];
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            return 1;
        }
    }

    // Validate arguments
    if (config_path.empty()) {
        std::cerr << "Error: --config is required\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (command.empty()) {
        std::cerr << "Error: command is required (up, collect, down, or status)\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // Load configuration
    harness::ClusterConfig config;
    try {
        config = harness::ClusterConfig::from_json(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << "\n";
        return 1;
    }

    // Create harness
    harness::Harness harness(config);

    // Execute command
    if (command == "up") {
        if (!harness.up(output_dir, test_config)) {
            return 1;
        }
    } else if (command == "collect") {
        std::string results_dir = harness.collect_results(output_dir);
        if (results_dir.empty()) {
            std::cerr << "Failed to collect results\n";
            return 1;
        }
        std::cout << "Results collected to: " << results_dir << "\n";
    } else if (command == "down") {
        if (!harness.down(output_dir)) {
            return 1;
        }
    } else if (command == "status") {
        harness.status();
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        std::cerr << "Valid commands: up, collect, down, status\n";
        return 1;
    }

    return 0;
}
