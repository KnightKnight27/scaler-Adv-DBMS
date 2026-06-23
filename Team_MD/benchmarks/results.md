# MiniDB Benchmarks

## 1. Table Scan vs Index Scan Latency

| Table Size (Rows) | SeqScan Latency (ns) | IndexScan Latency (ns) | Speedup |
|-------------------|----------------------|------------------------|---------|
| 100 | 259,300 | 24,700 | 10.50x |
| 1000 | 855,660 | 25,430 | 33.65x |
| 10000 | 6,561,560 | 27,060 | 242.48x |

![Latency Comparison](WhatsApp%20Image%202026-06-22%20at%2018.36.26.jpeg)

![Speedup Factor](WhatsApp%20Image%202026-06-22%20at%2018.36.26%20(1).jpeg)

## 2. MVCC Read Throughput & Non-Blocking Readers

Ran 10 concurrent readers alongside 1 writer (which slept for 200ms).

- **Total Table Scans Completed**: 193
- **Throughput**: 381.42 scans/sec
- **Max Reader Latency**: 38 ms

*(Since max reader latency is well under 200ms, readers were **not** blocked by the active writer).*

## 3. WAL / Recovery Overhead

| Mode | Time per Commit (ms) | Time per Commit (ns) |
|------|----------------------|----------------------|
| `fsync()` Enabled (Safe) | 0.8251 | 825,073 |
| `fsync()` Disabled (Fast) | 0.0027 | 2,658 |

*(Synchronous disk writes add significant latency, demonstrating the durability vs performance trade-off).*

