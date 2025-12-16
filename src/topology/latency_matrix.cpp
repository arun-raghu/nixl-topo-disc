#include "latency_matrix.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace nixl_topo {

LatencyMatrix::LatencyMatrix(size_t num_nodes)
    : num_nodes_(num_nodes)
    , data_(num_nodes * num_nodes) {
}

LatencyMatrix LatencyMatrix::from_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    std::vector<std::vector<double>> rows;
    std::string line;

    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty()) continue;

        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;

        std::vector<double> row;
        std::stringstream ss(line);
        std::string cell;

        while (std::getline(ss, cell, ',')) {
            // Trim whitespace from cell
            size_t cell_start = cell.find_first_not_of(" \t");
            size_t cell_end = cell.find_last_not_of(" \t");

            if (cell_start == std::string::npos) {
                throw std::runtime_error("Empty cell in CSV at row " +
                                        std::to_string(rows.size() + 1));
            }

            std::string trimmed = cell.substr(cell_start, cell_end - cell_start + 1);

            try {
                double value = std::stod(trimmed);
                row.push_back(value);
            } catch (const std::exception& e) {
                throw std::runtime_error("Invalid numeric value '" + trimmed +
                                        "' at row " + std::to_string(rows.size() + 1));
            }
        }

        if (!row.empty()) {
            rows.push_back(std::move(row));
        }
    }

    if (rows.empty()) {
        throw std::runtime_error("Empty CSV file: " + filepath);
    }

    // Verify matrix is square
    size_t n = rows.size();
    for (size_t i = 0; i < n; ++i) {
        if (rows[i].size() != n) {
            throw std::runtime_error("Matrix is not square: row " +
                                    std::to_string(i + 1) + " has " +
                                    std::to_string(rows[i].size()) +
                                    " columns, expected " + std::to_string(n));
        }
    }

    // Create matrix
    LatencyMatrix matrix(n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            matrix.at(i, j) = LatencyEntry(rows[i][j]);
        }
    }

    return matrix;
}

void LatencyMatrix::symmetrize() {
    for (size_t i = 0; i < num_nodes_; ++i) {
        for (size_t j = i + 1; j < num_nodes_; ++j) {
            double avg = (at(i, j).avg_ns + at(j, i).avg_ns) / 2.0;
            at(i, j).avg_ns = avg;
            at(j, i).avg_ns = avg;
        }
    }
}

const LatencyEntry& LatencyMatrix::at(size_t i, size_t j) const {
    if (i >= num_nodes_ || j >= num_nodes_) {
        throw std::out_of_range("Index out of range: (" +
                               std::to_string(i) + ", " + std::to_string(j) +
                               ") for matrix of size " + std::to_string(num_nodes_));
    }
    return data_[index(i, j)];
}

LatencyEntry& LatencyMatrix::at(size_t i, size_t j) {
    if (i >= num_nodes_ || j >= num_nodes_) {
        throw std::out_of_range("Index out of range: (" +
                               std::to_string(i) + ", " + std::to_string(j) +
                               ") for matrix of size " + std::to_string(num_nodes_));
    }
    return data_[index(i, j)];
}

double LatencyMatrix::distance(size_t i, size_t j) const {
    // Return average of bidirectional latencies
    return (at(i, j).avg_ns + at(j, i).avg_ns) / 2.0;
}

} // namespace nixl_topo
