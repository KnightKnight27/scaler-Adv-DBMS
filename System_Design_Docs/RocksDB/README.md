# RocksDB Architecture: Understanding LSM Trees Through Experiments

## 1. Problem Background

Traditional databases such as PostgreSQL and MySQL use B-Tree based storage engines. While B-Trees provide excellent read performance, they can become inefficient under heavy write workloads because updates frequently require random disk I/O, page modifications, and page splits.

Modern applications such as:

* Time-series databases
* Logging systems
* Streaming platforms
* Distributed storage engines
* Real-time analytics systems

generate significantly more writes than reads.

To address this challenge, Facebook developed **RocksDB**, a high-performance embedded key-value database based on the **Log Structured Merge Tree (LSM Tree)**.

The primary goals of RocksDB are:

* High write throughput
* Efficient SSD utilization
* Crash recovery support
* Acceptable read performance
* Scalable storage management

Today RocksDB is widely used in:

* MyRocks
* CockroachDB
* TiKV
* Kafka Streams
* Facebook infrastructure

---

# 2. Architecture Overview

## High-Level Architecture

```text
                 Client Write
                       |
                       v
                 Write Ahead Log
                       |
                       v
                    MemTable
                       |
                       v
             Immutable MemTable
                       |
                    Flush
                       |
                       v
                 SSTable (L0)
                       |
                  Compaction
                       |
          +------------+------------+
          |            |            |
          v            v            v
         L1           L2           Ln
```

Unlike B-Tree databases, RocksDB avoids modifying disk pages directly.

Instead:

1. Writes are appended to the WAL.
2. Data is stored in memory.
3. Memory is flushed sequentially to SSTables.
4. Background compaction reorganizes files.

This design minimizes random disk writes and improves write performance.

---

# 3. Internal Design

## 3.1 Write Ahead Log (WAL)

Before a write is acknowledged, RocksDB appends it to the WAL.

```text
Client Write
      |
      v
 WAL Append
      |
      v
 MemTable
```

### Experimental Evidence

Generated WAL file:

```text
000004.log
```

Observed during benchmarking:

```bash
ls -lh ~/rocksdb_test
```

Output:

```text
000004.log 14M
```

### Why WAL Exists

If the system crashes:

```text
Crash
   |
   v
Replay WAL
   |
   v
Recover MemTable
```

Durability is preserved even if data has not yet been flushed to SSTables.

---

## 3.2 MemTable

The MemTable is an in-memory sorted data structure.

Observed from benchmark output:

```text
Memtablerep: SkipListFactory
```

This indicates RocksDB uses a Skip List implementation.

### Benefits

* Fast inserts
* Sorted ordering
* Efficient lookups
* Concurrent-friendly structure

---

## 3.3 Immutable MemTable

When the active MemTable becomes full:

```text
MemTable
      |
      v
Immutable MemTable
```

A new MemTable is created immediately.

The previous MemTable becomes immutable and is flushed to disk in the background.

This design prevents write stalls.

---

## 3.4 SSTables

SSTable stands for:

```text
Sorted String Table
```

Properties:

* Immutable
* Sorted by key
* Optimized for sequential access

Data flow:

```text
MemTable
     |
 Flush
     |
     v
 SSTable
```

Because SSTables never change after creation, RocksDB avoids expensive random writes.

---

# 4. LSM Tree Storage Levels

RocksDB organizes SSTables into levels.

```text
Level 0
   |
Compaction
   |
Level 1
   |
Compaction
   |
Level 2
   |
Compaction
   |
Level N
```

### Characteristics

| Level | Description             |
| ----- | ----------------------- |
| L0    | Newly flushed SSTables  |
| L1    | Compacted files         |
| L2+   | Larger, optimized files |

As data moves downward:

* Read performance improves
* File overlap decreases
* Storage efficiency increases

---

# 5. Bloom Filters

Bloom Filters help RocksDB avoid unnecessary disk reads.

Without Bloom Filters:

```text
Read Request
      |
      v
Check SSTable 1
Check SSTable 2
Check SSTable 3
```

With Bloom Filters:

```text
Read Request
      |
      v
Bloom Filter
      |
      +--> Definitely Not Present
      |
      +--> Might Exist
```

### Benefits

* Reduced disk accesses
* Faster negative lookups
* Lower read amplification

### Trade-Off

* Additional memory usage
* False positives possible
* False negatives impossible

---

# 6. Write Path

Each write follows:

```text
Client
  |
  v
WAL
  |
  v
MemTable
  |
  v
Flush
  |
  v
SSTable
  |
  v
Compaction
```

### Why Writes Are Fast

Traditional B-Trees perform:

```text
Random Writes
```

LSM Trees perform:

```text
Sequential Writes
```

Sequential writes are significantly faster on both HDDs and SSDs.

---

# 7. Read Path

Read operations follow:

```text
Read Request
      |
      v
MemTable
      |
      v
Immutable MemTable
      |
      v
Bloom Filter
      |
      v
L0 SSTables
      |
      v
L1+
```

This introduces additional lookup work compared to B-Trees.

Read amplification is the primary trade-off accepted by LSM Trees.

---

# 8. Compaction

Because SSTables are immutable, RocksDB periodically merges files through compaction.

```text
L0 SSTables
       |
       v
 Merge
       |
       v
 L1 SSTables
```

### Benefits

* Removes duplicate versions
* Removes deleted records
* Improves read performance
* Reduces storage fragmentation

### Cost

* Additional CPU usage
* Additional disk writes
* Write amplification

---

# 9. Experimental Results

## Experiment 1: Random Write Benchmark

### Command

```bash
./db_bench \
  --db=$HOME/rocksdb_big \
  --benchmarks=fillrandom \
  --num=1000000
```

### Output

```text
fillrandom :
4.235 micros/op
236141 ops/sec
4.235 seconds
1000000 operations
26.1 MB/s
```

### Observation

RocksDB sustained approximately:

```text
236,141 writes/sec
```

This demonstrates the effectiveness of the LSM-tree write path.

---

## Experiment 2: Database Size

### Command

```bash
du -sh $HOME/rocksdb_big
```

### Output

```text
101M
```

### Observation

Benchmark inserted:

```text
1,000,000 entries
```

Estimated raw size:

```text
110.6 MB
```

Actual disk usage:

```text
101 MB
```

The reduction is due to:

```text
Compression: Snappy
```

observed in benchmark output.

---

## Experiment 3: SSTable Creation

### Command

```bash
ls -lh $HOME/rocksdb_big
```

### Observed SSTables

```text
000005.sst
000006.sst
000007.sst
000008.sst
000009.sst
000010.sst
000011.sst
000012.sst
000013.sst
000014.sst
000015.sst
```

Most SSTables were approximately:

```text
8.1 MB
```

Largest SSTable:

```text
000005.sst (26 MB)
```

### Observation

This confirms the complete LSM workflow:

```text
MemTable
      ↓
Flush
      ↓
SSTables
```

actually occurred during the benchmark.

---

## Experiment 4: Random Read Benchmark

### Command

```bash
./db_bench \
  --db=$HOME/rocksdb_test \
  --benchmarks=readrandom \
  --num=100000
```

### Output

```text
3517163 ops/sec
0.284 micros/op
```

### Observation

Read performance remained extremely high because:

* Dataset was relatively small
* Data remained cached
* Bloom Filters reduced unnecessary lookups

---

# 10. Amplification Analysis

## Write Amplification

A single logical write may be written multiple times.

```text
User Write
      |
      v
WAL
      |
      v
SSTable
      |
      v
Compaction
```

Therefore:

```text
1 Logical Write
      ≠
1 Physical Write
```

### Benefit

Excellent write throughput.

### Cost

Additional disk writes.

---

## Read Amplification

Reads may search:

```text
MemTable
Immutable MemTable
L0
L1
L2
...
```

Multiple structures must be checked before locating data.

Bloom Filters help reduce this overhead.

---

## Space Amplification

During compaction:

```text
Old SSTables
      +
New SSTables
```

temporarily coexist.

As a result:

```text
Actual Disk Usage
>
User Data Size
```

during compaction periods.

---

# 11. Design Trade-Offs

| Feature       | Benefit                    | Cost                |
| ------------- | -------------------------- | ------------------- |
| WAL           | Durability                 | Extra writes        |
| MemTable      | Fast inserts               | Memory usage        |
| SSTables      | Efficient storage          | Compaction required |
| Bloom Filters | Faster reads               | Additional memory   |
| Compaction    | Better read performance    | Write amplification |
| LSM Tree      | Excellent write throughput | Read amplification  |

---

# 12. Why LSM Trees Are Preferred for Write-Heavy Workloads

LSM Trees transform:

```text
Random Writes
      ↓
Sequential Writes
```

Benefits:

* Better SSD utilization
* Reduced page splits
* High insertion throughput
* Lower write latency
* Scalable storage growth

This makes RocksDB ideal for:

* Logging systems
* Time-series databases
* Streaming platforms
* Distributed storage engines

---

# 13. Why Compaction Can Become Expensive

Compaction repeatedly rewrites data.

Example:

```text
L0 -> L1 -> L2 -> L3
```

The same record may be rewritten multiple times.

Consequences:

* Increased CPU usage
* Increased disk bandwidth
* Increased write amplification

Compaction is often the largest operational cost in large-scale RocksDB deployments.

---

# 14. How Bloom Filters Improve Read Performance

Without Bloom Filters:

```text
Check Every SSTable
```

With Bloom Filters:

```text
Reject Missing Keys Immediately
```

Benefits:

* Fewer disk accesses
* Faster negative lookups
* Lower read amplification

Bloom Filters are one of the most important optimizations in RocksDB's read path.

---

# 15. Key Learnings

1. RocksDB is built around the LSM Tree architecture.
2. Writes first enter the WAL and MemTable.
3. Skip Lists are used for in-memory indexing.
4. Immutable MemTables allow concurrent writes and flushing.
5. SSTables are immutable disk structures.
6. Compaction is essential for maintaining read performance.
7. Bloom Filters reduce read amplification.
8. RocksDB achieved approximately **236K writes/sec** in the benchmark.
9. Multiple SSTables were generated, validating the LSM workflow.
10. RocksDB trades read complexity for extremely high write throughput.

---

# References

1. RocksDB Official Documentation
2. RocksDB Wiki
3. Facebook Engineering Blog
4. Designing Data-Intensive Applications — Martin Kleppmann
5. O'Neil et al. — The Log Structured Merge Tree (LSM Tree) Paper
