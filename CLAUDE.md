# Cluster Topology Discovery & Performance Measurement System

A C++ distributed agent system that measures memory transfer performance between cluster nodes using the NVIDIA NIXL framework.

- The docs directory contains several .md files describing the overall project design
- for NIXL API info, use CLAUDE.md in submodules/nixl
- for UCX API info, use CLAUDE.md in submodules/ucx
- for GDS API info, use CLAUDE.md in submodules/gds
- for GDR API info, use CLAUDE.md in submodules/gdr
---
## Module Structure

```
nixl-topo-disc/
├── src/
│   ├── agent/
│   │   ├── agent_main.cpp
│   │   ├── bootstrap.cpp
│   │   ├── nixl_comm.cpp
│   │   └── perf_tests.cpp
│   ├── controller/
│   │   ├── controller_main.cpp
│   │   ├── rendezvous.cpp
│   │   ├── test_manager.cpp
│   │   └── data_collector.cpp
│   ├── topology/
│   │   ├── tier_config.hpp/cpp
│   │   ├── latency_matrix.hpp/cpp
│   │   ├── dendrogram.hpp/cpp
│   │   ├── topology_graph.hpp/cpp
│   │   ├── topology_builder.hpp/cpp
│   │   └── code_structure.txt
│   ├── harness/
│   │   └── [placeholder]
│   └── common/
│       ├── nixl_wrapper.hpp
│       ├── protocol.hpp
│       └── types.hpp
├── include/
├── tests/
├── CLAUDE.md
└── CMakeLists.txt
```

---

## Build & Dependencies

- **Language:** C++17 or later
- **Build System:** CMake
- **Dependencies:**
  - NVIDIA NIXL SDK
  - Container orchestrator SDK (Docker SDK or Kubernetes client library)
  - (Optional) nlohmann/json for serialization

---

## Topology Module

When modifying code in `src/topology/`, update `src/topology/code_structure.txt` to reflect changes.
