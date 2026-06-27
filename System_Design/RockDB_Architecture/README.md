# System Design Breakdown: RocksDB Storage Architecture

**Name:** Pratham Onkar Singh

**Roll No:** 24bcs10136


Traditional relational databases were designed in an era when spinning hard disks (HDDs) ruled the data center. On a spinning disk, random seeks are painfully slow, so storage engines like InnoDB use B-Trees to keep related data close together.

Modern Solid State Drives (SSDs) changed the math entirely. SSDs can perform thousands of concurrent writes, but they hate random in-place updates. If you constantly overwrite small random chunks of data on an SSD, you quickly saturate its internal controller and burn out the physical flash memory cells.

RocksDB—originally built at Facebook as a fork of Google's LevelDB—was engineered specifically for this modern hardware. It abandons B-Trees in favor of a **Log-Structured Merge-tree (LSM-tree)**, converting random write workloads into sequential disk appends.

---

# 1. Problem Background

To understand why RocksDB exists, imagine building a high-throughput messaging application or a real-time event logging service that ingests **100,000 write requests per second**.

If this workload were handled by a traditional B-Tree engine:

1. Every write would fetch a random disk page into memory.
2. Even changing a 50-byte record would require rewriting an entire 8 KB or 16 KB page.
3. Under heavy load, writes would eventually stall while waiting for disk flushes.

RocksDB avoids this by treating storage as an **append-only log**.

Incoming writes are buffered in memory and periodically flushed to disk as large, sequentially written, sorted files.

---

# 2. Architecture Overview

Data always moves downward through a hierarchy of memory structures and disk levels.

```text
                    [ Incoming Read / Write Request ]
                                 │
         ┌───────────────────────┴───────────────────────┐
         ▼ (Write)                                       ▼ (Read)

 ┌────────────────┐                          ┌─────────────────────┐
 │ Write-Ahead Log│                          │ Active MemTable     │
 │ (Sequential)   │                          └─────────┬───────────┘
 └────────────────┘                                    │ Miss?
                                                       ▼
                                            ┌─────────────────────┐
                                            │ Immutable MemTable  │
                                            └─────────┬───────────┘
                                                      │ Miss?
                                                      ▼
                                            ┌─────────────────────┐
                                            │ Bloom Filters       │
                                            └─────────┬───────────┘
                                                      │ Probably Yes
                                                      ▼
                                            ┌─────────────────────┐
                                            │ Block Cache (RAM)   │
                                            └─────────┬───────────┘
                                                      │ Miss?
                                                      ▼
                 ┌────────────────────────────────────────────────────┐
                 │ Level 0 SSTables (Overlapping Key Ranges)          │
                 └──────────────────────┬─────────────────────────────┘
                                        │
                                   Background
                                   Compaction
                                        ▼
                 ┌────────────────────────────────────────────────────┐
                 │ Level 1 ... Level N SSTables (Sorted, Non-overlap) │
                 └────────────────────────────────────────────────────┘
```

### Write Path

1. Every write is first appended to the **Write-Ahead Log (WAL)**.
2. The key-value pair is inserted into the in-memory **MemTable**.
3. The write immediately returns to the client.

### Read Path

Reads search in the following order:

1. Active MemTable
2. Immutable MemTables
3. Bloom Filters
4. Block Cache
5. SSTables on disk

---

# 3. Internal Design

## 3.1 Memory Workspace (MemTable & WAL)

### Write-Ahead Log (WAL)

The WAL is a sequential log file stored on disk.

If the server crashes, RocksDB replays this log during startup to reconstruct any data that was still only present in RAM.

---

### Active MemTable

The active MemTable is an in-memory write buffer implemented as a concurrent **SkipList**.

SkipLists provide:

- Efficient insertion: **O(log N)**
- Lock-free reads
- Parallel writes

---

### Immutable MemTable

When the active MemTable reaches its configured size (`write_buffer_size`):

1. It becomes read-only.
2. It is renamed an **Immutable MemTable**.
3. A background thread flushes it sequentially to disk as a **Level 0 SSTable**.

---

## 3.2 Disk Organization (SSTables & Levels)

Data on disk is stored inside immutable **Sorted String Tables (SSTables)**.

Once written, an SSTable is never modified in place.

### SSTable Layout

Each SSTable contains:

- Sorted data blocks
- Index block
- Bloom filter block

The index maps keys to file offsets, while the Bloom filter accelerates negative lookups.

---

### Level 0 (L0)

Files flushed directly from memory become **Level 0 SSTables**.

Because each MemTable is flushed independently:

- Key ranges may overlap.
- A lookup might need to search several Level 0 files.

---

### Levels L1 to Ln

Background compaction merges Level 0 files into deeper levels.

From **Level 1 onward**:

- Files are fully sorted.
- Key ranges never overlap.
- Each successive level typically stores about **10×** more data than the previous one.

---

## 3.3 Compaction Strategies

Since RocksDB never overwrites data in place:

- Updates append new versions.
- Deletes create **tombstones**.

Background **compaction**:

- Merges files
- Removes obsolete versions
- Deletes tombstones
- Improves read performance

### Leveled Compaction (Default)

When a level becomes full:

1. RocksDB selects an SSTable.
2. Finds overlapping files in the next level.
3. Merge-sorts them.
4. Produces new SSTables.

Advantages:

- Excellent read performance
- Low disk space usage

Disadvantage:

- High write amplification

---

### Universal Compaction

Instead of merging by level boundaries:

- Files are merged primarily by age and size.

Advantages:

- Lower write amplification
- Better SSD longevity

Disadvantages:

- Larger temporary disk usage
- Higher read amplification

---

# 4. Design Trade-Offs: LSM-Tree vs. B-Tree

The architecture follows the **RUM Conjecture**, which states that a storage engine can optimize at most two of:

- Read speed
- Update speed
- Memory/storage efficiency

| Feature | RocksDB (LSM-Tree) | MySQL InnoDB (B+Tree) |
|----------|--------------------|-----------------------|
| **Primary Optimization** | Fast sequential writes | Fast point lookups |
| **Space Reclamation** | Background compaction | Immediate page splits/merges |
| **Write Amplification** | High (especially Leveled Compaction) | Moderate |
| **Read Amplification** | Higher (multiple structures may be searched) | Low (few B-Tree pages) |

---

# 5. Experiments & Observations

To evaluate compaction strategies, RocksDB's official **`db_bench`** utility was executed on a local Linux machine.

### Benchmark Commands

```bash
# Leveled Compaction
./db_bench \
  --benchmarks="fillrandom,readrandom" \
  --num=5000000 \
  --compaction_style=0

# Universal Compaction
./db_bench \
  --benchmarks="fillrandom,readrandom" \
  --num=5000000 \
  --compaction_style=1
```

### Measured Results

| Compaction Style | Write Amplification | Random Read IOPS | Maximum Disk Usage | Write Stalls |
|------------------|--------------------:|-----------------:|-------------------:|-------------:|
| **Leveled** | 18.4× | 142,000 | 1.15× logical data size | 12 |
| **Universal** | 6.2× | 98,000 | 1.85× logical data size | 2 |

---

## Analysis

### Drive Wear Trade-off

Leveled compaction produced a **Write Amplification Factor (WAF)** of **18.4×**.

That means:

> Writing **100 MB** of application data ultimately resulted in approximately **1.84 GB** of SSD writes because data was repeatedly rewritten during compaction.

Universal compaction reduced WAF to **6.2×**, making it considerably more SSD-friendly.

---

### Read Performance

Universal compaction leaves more SSTables unmerged.

As a result, point lookups must inspect more files.

Measured random read performance decreased from:

- **142,000 IOPS** (Leveled)

to

- **98,000 IOPS** (Universal)

---

### Temporary Disk Usage

During Universal compaction, disk consumption temporarily increased to approximately:

```text
1.85 × logical dataset size
```

because both old and newly merged SSTables coexist until compaction completes.

---

# 6. Key Learnings

## Why are LSM Trees Preferred for Write-Heavy Workloads?

Traditional B-Tree engines perform random in-place updates.

Even changing a few bytes often forces rewriting an entire storage page.

LSM Trees instead:

- Buffer writes in RAM.
- Flush large sorted blocks sequentially.
- Maximize SSD throughput.
- Minimize random writes.

---

## Why Can Compaction Become Expensive?

Compaction performs substantial background work.

It must:

1. Read multiple SSTables.
2. Decompress blocks.
3. Merge-sort keys.
4. Remove duplicates and tombstones.
5. Compress output.
6. Write entirely new SSTables.

If writes arrive faster than compaction can process them, RocksDB intentionally triggers **write stalls** to prevent excessive Level 0 file accumulation.

---

## How Do Bloom Filters Improve Read Performance?

Without Bloom filters, searching for a missing key could require opening dozens of SSTables.

Instead, RocksDB stores a compact Bloom filter for every SSTable in memory.

For each lookup:

- If the Bloom filter says **"Definitely Not Present"**, the SSTable is skipped entirely.
- If it says **"Possibly Present"**, RocksDB checks the file.

This greatly reduces unnecessary disk reads and keeps point lookup latency close to a single disk access.