# RocksDB (LSM Tree) Architecture & Performance Analysis

## Objective

The objective of this study is to understand the Log Structured Merge (LSM) tree architecture used in RocksDB and analyze its real-world performance characteristics using benchmark tools.

Focus areas include:

* MemTable and Immutable MemTable
* SSTables and Level structure
* Write Ahead Log (WAL)
* Bloom Filters
* Compaction
* Read and Write amplification
* Storage trade-offs

---

# 1. Overview of RocksDB Architecture

RocksDB is a high-performance key-value storage engine built on the LSM-tree model.

Architecture Flow:

```text id="r1"
Write → WAL → MemTable → Immutable MemTable → SSTable (L0 → L1 → L2 → ... Ln)
Read  → MemTable + SSTables + Bloom Filters
```

This design prioritizes sequential writes and high ingestion throughput.

---

# 2. Write Path Analysis

When data is written:

1. Write is first appended to WAL (Write Ahead Log)
2. Inserted into MemTable (in-memory structure)
3. When full, MemTable becomes Immutable MemTable
4. Flushed to SSTable on disk (L0)
5. Compaction moves data across levels (L0 → L1 → L2...)

Flow:

```text id="r2"
Client Write
   ↓
WAL (durability)
   ↓
MemTable (RAM)
   ↓
Immutable MemTable
   ↓
SSTable (L0)
   ↓
Compaction → Higher Levels
```

---

## Why LSM Trees are Write Optimized

* Sequential disk writes (no random updates)
* Batched memory flushes
* Efficient SSD usage
* WAL ensures durability

---

# 3. Read Path Analysis

Read process:

1. Check MemTable
2. Check Immutable MemTable
3. Check SSTables (L0 → Ln)
4. Use Bloom Filters to skip irrelevant files

Flow:

```text id="r3"
Read Request
   ↓
MemTable
   ↓
Immutable MemTable
   ↓
SSTables (Level-wise)
   ↓
Bloom Filter optimization
   ↓
Return Result
```

---

# 4. Bloom Filters

Bloom Filters are probabilistic structures used to avoid unnecessary disk reads.

Properties:

* Fast membership check
* False positives possible
* No false negatives

Impact:

* Reduces read amplification
* Avoids unnecessary SSTable scans

---

# 5. Compaction

Compaction merges SSTables across levels.

Purpose:

* Remove duplicate keys
* Delete obsolete versions
* Reduce storage waste
* Improve read performance

---

## Why Compaction is Required

Without compaction:

* Many outdated versions remain
* Read performance degrades
* Storage usage increases

---

## Why Compaction is Expensive

* High CPU usage
* Heavy disk I/O
* Temporary performance drops
* Background resource consumption

---

# 6. Benchmark Results (Actual Execution)

Benchmarks were executed using `db_bench`.

## System Information

* RocksDB Version: 8.9.1
* Entries: 1,000,000
* Key size: 16 bytes
* Value size: 100 bytes
* Compression: Snappy

---

## Write Performance (fillseq)

```text id="r4"
fillseq : 4.543 micros/op
          220,128 ops/sec
          24.4 MB/s
```

### Interpretation:

* Extremely fast sequential write throughput
* Confirms LSM-tree write optimization
* WAL + MemTable pipeline performs efficiently

---

## Read Performance (readrandom)

```text id="r5"
readrandom : 9.546 micros/op
             104,759 ops/sec
             11.6 MB/s
             (100% keys found)
```

### Interpretation:

* Moderate read latency compared to writes
* Successful full key retrieval
* Reads involve MemTable + SSTable lookup + Bloom Filter optimization

---

# 7. Write, Read, and Space Amplification

## Write Amplification

A single write is processed multiple times:

* WAL write
* MemTable insert
* SSTable flush
* Compaction rewrites

---

## Read Amplification

A single read may require:

* MemTable check
* Multiple SSTable level lookups
* Bloom Filter checks

---

## Space Amplification

Old versions remain until compaction removes them, increasing temporary storage usage.

---

# 8. Why LSM Trees are Used in Write-Heavy Systems

LSM trees are preferred because:

* Sequential write pattern
* High ingestion throughput
* Efficient batching
* Suitable for SSD storage

Used in:

* RocksDB
* Cassandra
* HBase
* LevelDB

---

# 9. Trade-offs in LSM Trees

| Feature     | Advantage                 | Disadvantage        |
| ----------- | ------------------------- | ------------------- |
| Writes      | Extremely fast            | Write amplification |
| Reads       | Acceptable                | Slower than B-Trees |
| Storage     | Efficient post-compaction | Temporary bloat     |
| Maintenance | Automated                 | Compaction overhead |

---

# 10. Key Insights from Experiment

From real benchmark execution:

1. Write throughput is significantly higher than read throughput.
2. LSM design strongly favors sequential ingestion workloads.
3. Read performance depends heavily on Bloom Filters and SSTable layout.
4. Compaction is necessary but expensive.
5. System performance is highly tunable via compaction strategy.

---

# Conclusion

RocksDB demonstrates the power of LSM-tree architecture for modern storage systems.

Key conclusions:

* LSM trees optimize write performance using sequential disk writes.
* Reads are optimized using Bloom Filters and multi-level SSTable lookup.
* Compaction is essential for maintaining performance but introduces overhead.
* Benchmark results confirm strong write performance (220K ops/sec) and moderate read performance (104K ops/sec).
* The architecture is ideal for write-heavy workloads such as logging, streaming, and distributed databases.