# RocksDB Architecture (LSM-Tree Storage)

**Author:** Abhiroop Sistu

**Roll Number:** 24BCS10287

I studied RocksDB, an embedded key-value store built on a Log-Structured Merge Tree (LSM-Tree). The key idea is the opposite of a B-Tree database: instead of updating data in place, RocksDB appends writes, buffers them in memory, and cleans them up later through compaction. This design makes writes very fast, but introduces read and space amplification.

---

## 1. Problem Background

Traditional B-Tree databases update pages in place, which often leads to:

- Slow random writes
- Increased SSD wear
- Poor performance for write-heavy workloads

Many modern systems such as:

- Logging platforms
- Metrics databases
- Time-series systems
- Message queues

are dominated by writes.

RocksDB (developed by Facebook as a fork of Google's LevelDB) addresses this by converting random writes into sequential writes while remaining an embedded library rather than a standalone server.

It is used in:

- MyRocks
- CockroachDB
- TiKV
- Flink State Backend
- Kafka Streams

---

## 2. Architecture Overview

```text
Write
  |
  +--> WAL (durability)
  |
  +--> Active MemTable (sorted, in memory)
            |
            | fills up
            v
      Immutable MemTable
            |
            | Flush
            v
      L0 SSTables

L0: [sst][sst][sst]      (overlapping ranges)
L1: [ sst ][ sst ]       (non-overlapping)
L2: [    sst    ]
L3: [       sst       ]
...
Ln: Largest and oldest level

Compaction merges SSTables downward
```

### Data Flow

```text
WAL
  ↓
MemTable
  ↓
Flush
  ↓
L0 SSTables
  ↓
Compaction
  ↓
L1 ... Ln
```

Unlike B-Trees, data is never modified in place.

---

## 3. Internal Design

### Write Path

A write follows these steps:

1. Append record to WAL
2. Insert record into active MemTable
3. Return success

The MemTable is typically implemented as a skip list.

When the MemTable becomes full:

```text
Active MemTable
        ↓
Immutable MemTable
        ↓
Background Flush
        ↓
L0 SSTable
```

A new MemTable immediately takes over, so writes never stop.

Benefits:

- Sequential disk writes
- High write throughput
- Low write latency

---

### SSTables

SSTable stands for:

```text
Sorted String Table
```

An SSTable is:

- Immutable
- Sorted by key
- Written once

Structure:

```text
SSTable
 ├── Data Blocks
 ├── Index Block
 └── Bloom Filter
```

Updates and deletes do not modify existing entries.

Instead:

```text
Put(key, value)
Put(key, newer_value)
Delete(key) -> Tombstone
```

Older versions remain until compaction removes them.

---

### Levels (L0 ... Ln)

#### Level 0

Created directly from MemTable flushes.

Properties:

- Files may overlap
- Newest data
- Fast writes

Example:

```text
L0:
[1-100]
[50-200]
[120-250]
```

Overlapping ranges require checking multiple files.

---

#### Levels 1+

Compaction moves data into deeper levels.

Properties:

- Non-overlapping key ranges
- Larger capacity
- Fewer files to check

Example:

```text
L1:
[1-100] [101-200]

L2:
[1-1000]

L3:
[1-10000]
```

Each level is roughly 10× larger than the previous one.

---

### Bloom Filters

Each SSTable contains a Bloom Filter.

A Bloom Filter answers:

```text
"Could this key be here?"
```

Results:

| Result | Meaning |
|----------|----------|
| No | Key definitely not present |
| Maybe | Key might be present |

Benefits:

- Skip unnecessary SSTables
- Reduce disk reads
- Improve point lookup performance

---

### Read Path

A lookup follows:

```text
MemTable
   ↓
Immutable MemTable
   ↓
L0 SSTables
   ↓
L1 SSTables
   ↓
L2 SSTables
   ↓
...
```

At each stage:

1. Bloom filter checked
2. Candidate SSTables searched
3. Newest version returned

If the newest record is a tombstone:

```text
Key = Deleted
```

A read may touch multiple files, creating read amplification.

---

### Compaction

Compaction is RocksDB's background cleanup mechanism.

Functions:

- Merge SSTables
- Remove obsolete versions
- Remove tombstones
- Reorganize levels

Example:

```text
Before

L0:
A=1
A=2
A=3

After Compaction

L1:
A=3
```

Only the newest value survives.

---

### Compaction Strategies

#### Leveled Compaction

Default mode.

Advantages:

- Low read amplification
- Low space amplification

Disadvantages:

- Higher write amplification

#### Universal (Tiered) Compaction

Advantages:

- Lower write amplification

Disadvantages:

- Higher read amplification
- Higher space amplification

---

## 4. Design Trade-Offs

LSM Trees are usually analyzed through three amplification metrics.

### Write Amplification

```text
Bytes Written to Disk
---------------------
Bytes Written by User
```

Compaction rewrites data multiple times.

Higher values mean more SSD wear.

---

### Read Amplification

```text
Number of places checked
for one lookup
```

A key may exist in:

- MemTable
- L0
- L1
- L2
- ...

More levels increase read cost.

---

### Space Amplification

```text
Disk Space Used
---------------
Live Data Size
```

Old versions and tombstones occupy extra space until compaction.

---

### The RUM Trade-Off

You cannot simultaneously minimize:

- Read Amplification
- Update (Write) Amplification
- Memory Usage

Improving one usually worsens another.

---

### Leveled vs Universal

| Metric | Leveled | Universal |
|----------|----------|------------|
| Write Amplification | Higher | Lower |
| Read Amplification | Lower | Higher |
| Space Amplification | Lower | Higher |
| Best For | Read-heavy workloads | Write-heavy workloads |

---

### LSM vs B-Tree

| Feature | LSM Tree | B-Tree |
|----------|----------|---------|
| Writes | Sequential | Random |
| Reads | More expensive | Faster |
| Background Cleanup | Compaction | Minimal |
| SSD Friendliness | Excellent | Moderate |
| Write Throughput | Very High | Lower |

A downside of LSM Trees is that compaction can fall behind, causing write stalls.

---

## 5. Experiments / Observations

**Environment:** RocksDB 11.1.1

I built a custom C++ benchmark using the RocksDB API.

Workload:

- 2,000,000 keys
- 119-byte values
- Random insertion order
- Compression disabled
- 1,000,000 point reads

Logical data size:

```text
~238 MB
```

---

### LSM Structure After Compaction

```text
L0: 1 file

L1:
4 files
15 MB

L2:
42 files
159 MB

L3:
12 files
47 MB
```

Totals:

```text
59 files
224 MB on disk
```

---

### Write Amplification

Measured using RocksDB's internal statistics.

| Compaction Strategy | Write Amplification | Bytes Written |
|---------------------|---------------------|---------------|
| Leveled | 5.1× | ~1.2 GB |
| Universal | 3.4× | ~0.8 GB |

Observation:

Universal compaction rewrote significantly less data.

Leveled compaction rewrote each byte approximately five times as data moved through levels.

---

### Bloom Filter Impact

Workload:

```text
1,000,000 point reads
```

Results:

| Bloom Filter | Read Time | Throughput |
|-------------|------------|------------|
| ON | 4.3 s | 235K ops/s |
| OFF | 6.4 s | 157K ops/s |

Additional statistic:

```text
Files skipped by Bloom filters:
1,599,895
```

Observation:

Bloom filters improved lookup throughput by roughly 1.5× by avoiding unnecessary SSTable reads.

---

## 6. Key Learnings

- LSM Trees are fundamentally write-optimized.
- Writes become memory inserts plus sequential appends.
- Compaction is the cost paid later for fast writes.
- Bloom filters are essential for keeping reads efficient.
- Different compaction strategies optimize different workloads.
- There is no free lunch: improving reads, writes, or space efficiency usually harms another metric (RUM trade-off).
- Immutable files simplify recovery and concurrency.
- Most of RocksDB's complexity exists to manage background cleanup and amplification.

---

## References

1. RocksDB Wiki
   - Overview
   - MemTables
   - Bloom Filters
   - Leveled Compaction
   - Universal Compaction

2. O'Neil et al., *The Log-Structured Merge Tree (1996)*

3. Athanassoulis et al., *The RUM Conjecture*

4. Experiments run using a custom C++ benchmark on RocksDB 11.1.1
   - 2 million key inserts
   - Full compaction
   - 1 million point reads
   - Analysis using `rocksdb.stats`