/// topology_viz - Command-line tool for visualizing topology graphs
///
/// Usage:
///   topology_viz <input.csv> [options]
///
/// Options:
///   -o, --output <file>   Output file (default: topology.png)
///   -f, --format <fmt>    Output format: png, svg, pdf, dot (default: from extension)
///   -l, --layout <eng>    GraphViz layout: dot, neato, fdp, circo (default: dot)
///   -c, --config <file>   JSON config file for tier thresholds
///   -a, --adjacency       Output adjacency list to stdout
///   -j, --json            Output JSON to stdout
///   -h, --help            Show this help message

#include "topology_builder.hpp"
#include "latency_matrix.hpp"

#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " <input.csv> [options]\n"
              << "\n"
              << "Visualize cluster topology from a latency matrix.\n"
              << "\n"
              << "Arguments:\n"
              << "  input.csv           CSV file containing NxN latency matrix (nanoseconds)\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output <file> Output file (default: topology.png)\n"
              << "  -f, --format <fmt>  Output format: png, svg, pdf, dot (default: from extension)\n"
              << "  -l, --layout <eng>  GraphViz layout: dot, neato, fdp, circo (default: dot)\n"
              << "  -c, --config <file> JSON config file for tier thresholds\n"
              << "  -a, --adjacency     Output adjacency list to stdout\n"
              << "  -j, --json          Output JSON to stdout\n"
              << "  -h, --help          Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << " latency.csv                    # Generate topology.png\n"
              << "  " << program << " latency.csv -o graph.svg       # Generate SVG\n"
              << "  " << program << " latency.csv -o graph.dot       # Generate DOT file only\n"
              << "  " << program << " latency.csv -a                 # Print adjacency list\n"
              << "  " << program << " latency.csv -c config.json     # Use custom thresholds\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse arguments
    std::string input_file;
    std::string output_file = "topology.png";
    std::string layout = "dot";
    std::string config_file;
    bool print_adjacency = false;
    bool print_json = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires a file path\n";
                return 1;
            }
            output_file = argv[++i];
        } else if (arg == "-l" || arg == "--layout") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --layout requires an engine name\n";
                return 1;
            }
            layout = argv[++i];
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --config requires a file path\n";
                return 1;
            }
            config_file = argv[++i];
        } else if (arg == "-a" || arg == "--adjacency") {
            print_adjacency = true;
        } else if (arg == "-j" || arg == "--json") {
            print_json = true;
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return 1;
        } else {
            if (input_file.empty()) {
                input_file = arg;
            } else {
                std::cerr << "Error: Multiple input files specified\n";
                return 1;
            }
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: No input CSV file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    // Load latency matrix
    nixl_topo::LatencyMatrix matrix;
    try {
        matrix = nixl_topo::LatencyMatrix::from_csv(input_file);
        matrix.symmetrize();
    } catch (const std::exception& e) {
        std::cerr << "Error loading CSV: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Loaded " << matrix.num_nodes() << "x" << matrix.num_nodes()
              << " latency matrix from " << input_file << "\n";

    // Build topology
    nixl_topo::TopologyBuilder builder;
    if (!config_file.empty()) {
        builder = nixl_topo::TopologyBuilder::from_config(config_file);
        std::cout << "Using config from " << config_file << "\n";
    }

    nixl_topo::TopologyGraph graph = builder.build(matrix);

    std::cout << "Built topology: " << graph.num_physical_nodes() << " physical nodes, "
              << graph.num_hidden_nodes() << " hidden nodes, "
              << graph.num_edges() << " edges\n";

    // Output adjacency list if requested
    if (print_adjacency) {
        std::cout << "\n" << graph.to_adjacency_list();
    }

    // Output JSON if requested
    if (print_json) {
        std::cout << "\n" << graph.to_json();
    }

    // Determine output format
    std::string format;
    size_t dot_pos = output_file.rfind('.');
    if (dot_pos != std::string::npos) {
        format = output_file.substr(dot_pos + 1);
    }

    // Generate output
    if (format == "dot" || format == "gv") {
        // Just write DOT file
        if (graph.write_dot_file(output_file)) {
            std::cout << "DOT file written to " << output_file << "\n";
        } else {
            std::cerr << "Error writing DOT file\n";
            return 1;
        }
    } else {
        // Render using GraphViz
        std::cout << "Rendering with GraphViz (" << layout << " layout)...\n";
        if (graph.render_to_file(output_file, layout)) {
            std::cout << "Graph rendered to " << output_file << "\n";
        } else {
            std::cerr << "Error rendering graph. Is GraphViz installed?\n";
            std::cerr << "  Install with: sudo apt install graphviz\n";
            return 1;
        }
    }

    return 0;
}
