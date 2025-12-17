# Interpreting Results

## Output Files

| File | Description | Format |
|------|-------------|--------|
| `/tmp/latency_matrix.csv` | NxN round-trip latency matrix | `node_i,node_j,latency_ns` |
| `/tmp/bandwidth_matrix.csv` | NxN peak bandwidth matrix | `node_i,node_j,bandwidth_mbps` |
| `/tmp/bandwidth_detailed.csv` | Bandwidth at each message size | `initiator,responder,msg_size,bandwidth_mbps` |
| `/tmp/latency_detailed.csv` | Latency at each message size | `initiator,responder,msg_size,avg_latency_ns,min_latency_ns,max_latency_ns` |

## Sample Output

**Latency Matrix:**
```
0,1,5074
1,0,5089
```
Each row shows: source node, destination node, RTT in nanoseconds.

**Bandwidth Detailed:**
```
initiator,responder,msg_size,bandwidth_mbps
0,1,65536,8521.3
0,1,1048576,12847.2
```
