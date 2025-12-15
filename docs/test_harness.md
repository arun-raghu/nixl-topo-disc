
---

## 4. Test Harness

Containerized deployment for development, testing, and CI/CD.

### 4.a Minimal Container Deployment (Phase 1)

Basic setup to run agents and controller in containers on a single host or cluster.

#### Container Images

```dockerfile
# Dockerfile.agent
FROM nvidia/cuda:12.0-base-ubuntu22.04

RUN apt-get update && apt-get install -y \
    libibverbs-dev \
    librdmacm-dev \
    && rm -rf /var/lib/apt/lists/*

# Install NIXL SDK (assumes available)
COPY nixl-sdk/ /opt/nixl/
ENV LD_LIBRARY_PATH=/opt/nixl/lib:$LD_LIBRARY_PATH

COPY build/topo-agent /usr/local/bin/
ENTRYPOINT ["topo-agent"]
```

```dockerfile
# Dockerfile.controller
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libibverbs-dev \
    librdmacm-dev \
    && rm -rf /var/lib/apt/lists/*

COPY nixl-sdk/ /opt/nixl/
ENV LD_LIBRARY_PATH=/opt/nixl/lib:$LD_LIBRARY_PATH

COPY build/topo-controller /usr/local/bin/
ENTRYPOINT ["topo-controller"]
```

#### Minimal Docker Compose (Single Host)

```yaml
# docker-compose.yml
version: '3.8'

services:
  controller:
    build:
      context: .
      dockerfile: Dockerfile.controller
    container_name: topo-controller
    network_mode: host  # Required for RDMA/NIXL
    privileged: true    # Required for RDMA device access
    volumes:
      - /dev/infiniband:/dev/infiniband  # RDMA devices
    environment:
      - CONTROLLER_PORT=50000
      - NUM_AGENTS=4
    command: ["--port", "50000", "--agents", "4"]

  agent-0:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-0
    network_mode: host
    privileged: true
    volumes:
      - /dev/infiniband:/dev/infiniband
    depends_on:
      - controller
    environment:
      - CONTROLLER_HOST=127.0.0.1
      - CONTROLLER_PORT=50000
      - BUFFER_SIZE_MB=256
    command: ["--controller", "127.0.0.1:50000", "--buffer-mb", "256"]

  agent-1:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-1
    network_mode: host
    privileged: true
    volumes:
      - /dev/infiniband:/dev/infiniband
    depends_on:
      - controller
    command: ["--controller", "127.0.0.1:50000", "--buffer-mb", "256"]

  # Add more agents as needed (agent-2, agent-3, ...)
```

#### Running the Test Harness

```bash
# Build images
docker-compose build

# Start controller and agents
docker-compose up -d

# Check logs
docker-compose logs -f controller
docker-compose logs -f agent-0

# Run tests via controller CLI
docker exec topo-controller topo-ctl run-test --type PAIRWISE_LATENCY

# Collect results
docker exec topo-controller topo-ctl get-results --format json > results.json

# Shutdown
docker-compose down
```

#### Kubernetes Deployment (Cluster)

```yaml
# k8s/controller-deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: topo-controller
spec:
  replicas: 1
  selector:
    matchLabels:
      app: topo-controller
  template:
    metadata:
      labels:
        app: topo-controller
    spec:
      hostNetwork: true  # Required for RDMA
      containers:
      - name: controller
        image: topo-disc/controller:latest
        securityContext:
          privileged: true
        args: ["--port", "50000", "--agents", "8"]
        resources:
          limits:
            rdma/rdma_shared_device_a: 1  # RDMA device plugin
---
# k8s/agent-daemonset.yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: topo-agent
spec:
  selector:
    matchLabels:
      app: topo-agent
  template:
    metadata:
      labels:
        app: topo-agent
    spec:
      hostNetwork: true
      containers:
      - name: agent
        image: topo-disc/agent:latest
        securityContext:
          privileged: true
        args: ["--controller", "$(CONTROLLER_HOST):50000"]
        env:
        - name: CONTROLLER_HOST
          value: "controller-node-ip"
        resources:
          limits:
            rdma/rdma_shared_device_a: 1
            nvidia.com/gpu: 1  # If GPU memory testing
```

---

### 4.b Simulated Network Topologies (Phase 2)

> **Implementation Note:** This is a **later phase feature**. Implement basic containerized deployment first.

Use Docker networking and traffic control to simulate different network topologies and bandwidth constraints.

#### Docker Compose with Custom Networks

```yaml
# docker-compose.topology.yml
version: '3.8'

networks:
  # Simulate rack-local network (high bandwidth, low latency)
  rack-0:
    driver: bridge
    driver_opts:
      com.docker.network.driver.mtu: 9000  # Jumbo frames
    ipam:
      config:
        - subnet: 172.20.0.0/24

  rack-1:
    driver: bridge
    driver_opts:
      com.docker.network.driver.mtu: 9000
    ipam:
      config:
        - subnet: 172.20.1.0/24

  # Simulate cross-rack network (lower bandwidth, higher latency)
  spine:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.100.0/24

services:
  controller:
    build:
      context: .
      dockerfile: Dockerfile.controller
    networks:
      - spine
    # ...

  # Rack 0 agents
  agent-0:
    build:
      context: .
      dockerfile: Dockerfile.agent
    networks:
      - rack-0
      - spine
    # ...

  agent-1:
    networks:
      - rack-0
      - spine
    # ...

  # Rack 1 agents
  agent-2:
    networks:
      - rack-1
      - spine
    # ...

  agent-3:
    networks:
      - rack-1
      - spine
    # ...
```

#### Traffic Control for Bandwidth/Latency Simulation

```bash
#!/bin/bash
# setup-tc.sh - Apply traffic shaping to simulate network tiers

# Simulate cross-rack latency (5ms) and bandwidth limit (10Gbps)
setup_spine_tc() {
    local container=$1
    local iface="eth1"  # spine network interface

    docker exec $container tc qdisc add dev $iface root handle 1: htb default 10
    docker exec $container tc class add dev $iface parent 1: classid 1:10 htb \
        rate 10gbit ceil 10gbit
    docker exec $container tc qdisc add dev $iface parent 1:10 handle 10: \
        netem delay 5ms 0.5ms  # 5ms +/- 0.5ms jitter
}

# Simulate intra-rack (high bandwidth, low latency)
setup_rack_tc() {
    local container=$1
    local iface="eth0"  # rack network interface

    docker exec $container tc qdisc add dev $iface root handle 1: htb default 10
    docker exec $container tc class add dev $iface parent 1: classid 1:10 htb \
        rate 100gbit ceil 100gbit
    docker exec $container tc qdisc add dev $iface parent 1:10 handle 10: \
        netem delay 0.1ms  # 100us latency
}

# Apply to all containers
for i in 0 1 2 3; do
    setup_rack_tc "topo-agent-$i"
    setup_spine_tc "topo-agent-$i"
done

echo "Traffic shaping applied"
```

#### Topology Scenarios

```yaml
# scenarios/two-rack.yml
# Two racks with 4 agents each, spine interconnect

topology:
  racks:
    - name: rack-0
      agents: [0, 1, 2, 3]
      intra_rack:
        bandwidth_gbps: 100
        latency_us: 100
    - name: rack-1
      agents: [4, 5, 6, 7]
      intra_rack:
        bandwidth_gbps: 100
        latency_us: 100
  spine:
    bandwidth_gbps: 25
    latency_us: 5000  # 5ms

---
# scenarios/nvlink-cluster.yml
# Simulates NVLink-connected GPUs within nodes

topology:
  nodes:
    - name: node-0
      agents: [0, 1, 2, 3]  # 4 GPUs per node
      intra_node:
        bandwidth_gbps: 600  # NVLink
        latency_us: 2
    - name: node-1
      agents: [4, 5, 6, 7]
      intra_node:
        bandwidth_gbps: 600
        latency_us: 2
  network:
    bandwidth_gbps: 100  # InfiniBand between nodes
    latency_us: 1000
```

#### Scenario Runner

```bash
#!/bin/bash
# run-scenario.sh <scenario-file>

SCENARIO=$1

# Parse scenario and generate docker-compose
python3 scripts/gen-compose.py --scenario $SCENARIO --output docker-compose.gen.yml

# Start containers
docker-compose -f docker-compose.gen.yml up -d

# Apply traffic shaping based on scenario
python3 scripts/apply-tc.py --scenario $SCENARIO

# Run topology discovery
docker exec topo-controller topo-ctl run-test --type PAIRWISE_LATENCY

# Compare discovered topology with expected
docker exec topo-controller topo-ctl get-topology --format json > discovered.json
python3 scripts/compare-topology.py --expected $SCENARIO --discovered discovered.json

# Cleanup
docker-compose -f docker-compose.gen.yml down
```

---

### 4.c Non-Privileged Testing with UCX

NIXL uses UCX (Unified Communication X) as a transport layer. UCX supports multiple transports, some of which do not require privileged access.

#### UCX Transport Options

| Transport | Env Var | Privileges | Performance | Use Case |
|-----------|---------|------------|-------------|----------|
| `tcp` | `UCX_TLS=tcp` | None | Moderate (~10 Gbps) | Cross-host testing, CI/CD |
| `shm` | `UCX_TLS=shm` | None | Good (~50+ GB/s) | Single-host testing |
| `rc` (RDMA RC) | `UCX_TLS=rc` | Privileged | Highest | Production |
| `dc` (RDMA DC) | `UCX_TLS=dc` | Privileged | Highest | Production |

#### Running Non-Privileged Tests

**Environment variable configuration:**

```bash
# Force UCX to use only TCP transport (no RDMA, no privileges needed)
export UCX_TLS=tcp

# For single-host testing, use shared memory for better performance
export UCX_TLS=shm

# Combine for flexibility (use shm when possible, fall back to tcp)
export UCX_TLS=shm,tcp

# Disable RDMA transports explicitly
export UCX_TLS=tcp,shm
export UCX_NET_DEVICES=all  # Use all network devices
```

**Docker Compose for non-privileged testing:**

```yaml
# docker-compose.unprivileged.yml
version: '3.8'

services:
  controller:
    build:
      context: .
      dockerfile: Dockerfile.controller
    container_name: topo-controller
    # NO privileged: true
    # NO network_mode: host
    networks:
      - topo-net
    environment:
      - UCX_TLS=tcp,shm           # Force non-RDMA transports
      - UCX_LOG_LEVEL=warn
      - CONTROLLER_PORT=50000
      - NUM_AGENTS=4
    ports:
      - "50000:50000"
    command: ["--port", "50000", "--agents", "4"]

  agent-0:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-0
    networks:
      - topo-net
    environment:
      - UCX_TLS=tcp,shm
      - UCX_LOG_LEVEL=warn
      - CONTROLLER_HOST=controller
      - CONTROLLER_PORT=50000
    depends_on:
      - controller
    command: ["--controller", "controller:50000"]

  agent-1:
    build:
      context: .
      dockerfile: Dockerfile.agent
    container_name: topo-agent-1
    networks:
      - topo-net
    environment:
      - UCX_TLS=tcp,shm
    depends_on:
      - controller
    command: ["--controller", "controller:50000"]

networks:
  topo-net:
    driver: bridge
```

**Running non-privileged tests:**

```bash
# Start without any special permissions
docker-compose -f docker-compose.unprivileged.yml up -d

# Verify UCX is using TCP/SHM
docker exec topo-agent-0 ucx_info -d  # Shows available transports

# Run tests - NIXL works via UCX TCP, no RDMA required
docker exec topo-controller topo-ctl run-test --type PAIRWISE_LATENCY

# Results are valid for functional testing
# (Performance numbers won't match real RDMA)
docker exec topo-controller topo-ctl get-results
```

#### Unit Tests (No UCX/NIXL Required)

Pure algorithm tests run without any network stack:

```bash
# Build and run unit tests (no special permissions)
cmake -B build
cmake --build build --target unit_tests

./build/tests/unit_tests --gtest_filter="Clustering*"
./build/tests/unit_tests --gtest_filter="GraphConstruction*"
./build/tests/unit_tests --gtest_filter="Statistics*"
```

#### Testing Mode Summary

| Mode | UCX_TLS | Privileges | Containers | Use Case |
|------|---------|------------|------------|----------|
| Unit tests | N/A | None | N/A | Algorithm validation |
| UCX TCP | `tcp` | None | Standard | CI/CD, integration tests |
| UCX SHM | `shm` | None | Standard | Single-host functional tests |
| UCX RDMA | `rc,dc` | Privileged | Privileged + hostNetwork | Production measurements |

**Important Notes:**
- Performance measurements from TCP/SHM modes are not representative of real RDMA performance
- Use TCP/SHM modes for functional correctness testing only
- Topology detection algorithms will work but latency thresholds may need adjustment for simulated transports
- For accurate performance characterization, full RDMA mode is required

---

### 4.d Test Harness Summary

| Phase | Feature | Purpose |
|-------|---------|---------|
| **Phase 1** | Basic containers (RDMA) | Production, real measurements |
| **Phase 1** | UCX TCP/SHM mode | Non-privileged functional testing |
| **Phase 1** | Unit tests | Algorithm validation |
| **Phase 2** | Custom networks + TC | Simulate multi-rack topologies |
| **Phase 2** | Scenario runner | Automated topology validation |

#### Phase 1 Deliverables
- `Dockerfile.agent`, `Dockerfile.controller`
- `docker-compose.yml` for single-host deployment
- Basic Kubernetes manifests

#### Phase 2 Deliverables
- `docker-compose.topology.yml` with custom networks
- Traffic control scripts (`setup-tc.sh`)
- Scenario definitions (YAML)
- Topology comparison tools

---