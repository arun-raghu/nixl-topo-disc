# Cluster Topology Discovery & Performance Measurement System

A C++ distributed agent system that measures memory transfer performance between cluster nodes using the NVIDIA NIXL framework.

The docs directory contains .md files describing the overall project design

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
│   │   └── [placeholder]
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
