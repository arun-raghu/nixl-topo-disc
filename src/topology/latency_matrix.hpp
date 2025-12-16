#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nixl_topo {

// =============================================================================
// Latency Entry
// =============================================================================

struct LatencyEntry {
    double avg_ns = 0.0;      // Average latency in nanoseconds
    double min_ns = 0.0;      // Minimum observed latency
    double max_ns = 0.0;      // Maximum observed latency
    double stddev_ns = 0.0;   // Standard deviation
    uint32_t samples = 0;     // Number of samples

    LatencyEntry() = default;
    explicit LatencyEntry(double avg) : avg_ns(avg), min_ns(avg), max_ns(avg) {}
};

// =============================================================================
// Latency Matrix
// =============================================================================

/// NxN symmetric matrix of latencies between nodes
class LatencyMatrix {
public:
    LatencyMatrix() = default;
    explicit LatencyMatrix(size_t num_nodes);

    /// Load from CSV file
    /// Format: Each line is one row of the matrix, comma-separated values
    /// Values are interpreted as average latency in nanoseconds
    /// @param filepath Path to CSV file
    /// @return Loaded LatencyMatrix
    /// @throws std::runtime_error if file cannot be opened or parsed
    static LatencyMatrix from_csv(const std::string& filepath);

    /// Symmetrize the matrix by averaging L[i][j] and L[j][i]
    void symmetrize();

    /// Access latency entry between nodes i and j
    const LatencyEntry& at(size_t i, size_t j) const;
    LatencyEntry& at(size_t i, size_t j);

    /// Get distance metric for clustering (uses avg_ns)
    /// Returns averaged bidirectional latency: (L[i][j] + L[j][i]) / 2
    double distance(size_t i, size_t j) const;

    /// Get number of nodes in the matrix
    size_t num_nodes() const { return num_nodes_; }

    /// Check if matrix is empty
    bool empty() const { return num_nodes_ == 0; }

    /// Get raw data vector (row-major order)
    const std::vector<LatencyEntry>& data() const { return data_; }

private:
    size_t num_nodes_ = 0;
    std::vector<LatencyEntry> data_;  // Row-major storage: data_[i * num_nodes_ + j]

    size_t index(size_t i, size_t j) const { return i * num_nodes_ + j; }
};

} // namespace nixl_topo
