# RocksDB Architecture

## 1. Problem Background

RocksDB is an embeddable persistent key-value storage engine based on the Log-Structured Merge-tree design. It was created for workloads where write throughput, storage efficiency tuning, and predictable performance on fast storage are important.

Traditional B-tree systems update pages in place. That works well for point lookups and range scans, but random writes can become expensive because many small updates touch many different disk pages. RocksDB uses an LSM-tree approach: writes are first appended and stored in memory, then flushed into immutable sorted files, and later compacted in the background.

The main problem RocksDB solves is high-throughput durable writes with tunable read, write, and space amplification.

## 2. Architecture Overview

```text
Write path

Put/Delete
   |
   v
Write-ahead log
   |
   v
MemTable
   |
   v
Immutable MemTable
   |
   v
Flush
   |
   v
Level-0 SSTables
   |
   v
Compaction
   |
   v
Lower-level SSTables
```

```text
Read path

Get(key)
  |
  +--> MemTable
  +--> Immutable MemTables
  +--> Block cache
  +--> Bloom filters
  +--> SSTables from L0 to lower levels
```

RocksDB is not a SQL database. It provides ordered key-value operations. Higher-level systems can build SQL, document, time-series, or metadata layers on top of it.

## 3. Internal Design

### MemTable

The MemTable is an in-memory sorted structure that receives new writes. When a write arrives, RocksDB records it in the WAL for durability and inserts it into the MemTable. Reads check the MemTable first because it contains the newest values.

The MemTable absorbs random writes in memory, which is the first major advantage of the LSM design.

### Immutable MemTable

When a MemTable reaches its configured size, it becomes immutable. New writes go to a fresh MemTable, while the immutable MemTable is flushed to disk. This allows writes to continue while background flushing happens.

### SSTables

An SSTable is an immutable sorted-string table file. It stores sorted key-value data plus metadata such as indexes and optional filters. Because SSTables are immutable, writes do not update them in place. Instead, newer versions of keys are written to newer structures, and compaction later removes overwritten or deleted entries.

This immutability makes writes sequential and recovery simpler, but it means the same key may temporarily exist in multiple places.

### WAL

The write-ahead log protects recent writes before they are flushed into SSTables. If the process crashes, RocksDB can replay the WAL to rebuild MemTables that had not yet been persisted as SSTables.

### Levels: L0 To Ln

Flushed SSTables first land in Level 0. L0 files may have overlapping key ranges. Lower levels are organized to reduce overlap depending on the compaction strategy. In leveled compaction, lower levels are larger and more organized, which improves read efficiency but increases write amplification because data is rewritten during compaction.

### Bloom Filters

Bloom filters are probabilistic membership structures. They can quickly answer "definitely not present" for a key. This helps avoid unnecessary SSTable reads during point lookups. A Bloom filter can have false positives, but it should not have false negatives.

### Compaction

Compaction is the process of merging SSTables, discarding overwritten values and tombstones, and moving data to lower levels. It is required because LSM writes create many immutable files over time. Without compaction, read amplification and space amplification would grow too much.

Compaction is also the main cost of LSM systems. It consumes CPU and I/O, rewrites data, and can affect latency if not tuned carefully.

### Read Path

A point read checks the newest structures first: active MemTable, immutable MemTables, then SSTables. Bloom filters and block indexes reduce unnecessary disk reads. Range scans may need to merge sorted data from multiple levels.

### Write Path

A write is appended to the WAL, inserted into the MemTable, later flushed to an SSTable, and eventually compacted into lower levels. This makes foreground writes fast, but the total system cost includes background compaction.

## 4. Design Trade-Offs

| Design choice | Benefit | Cost |
| --- | --- | --- |
| LSM-tree storage | High write throughput | Reads may check multiple structures |
| Immutable SSTables | Sequential writes and simpler file management | Compaction required |
| WAL + MemTable | Durable low-latency writes | WAL replay needed after crash |
| Bloom filters | Faster negative point lookups | Extra memory and false positives |
| Leveled compaction | Lower read and space amplification | Higher write amplification |
| Universal compaction | Lower write amplification for some workloads | Higher read or space amplification |

The core RocksDB trade-off is amplification. Tuning reduces one type of amplification by increasing another:

- Write amplification: how many times data is rewritten.
- Read amplification: how many places must be checked to answer a read.
- Space amplification: how much extra storage is used by obsolete versions and overlapping files.

## 5. Experiments / Observations

### Benchmark Exercise

RocksDB includes benchmark tooling such as `db_bench`. A useful experiment is to compare write-heavy and read-heavy workloads under different compaction settings.

Example benchmark plan:

```text
1. Run fillrandom to load many keys.
2. Run readrandom to measure point lookup behavior.
3. Run overwrite or updaterandom to create obsolete versions.
4. Compare stats with Bloom filters enabled and disabled.
5. Compare leveled and universal compaction for write amplification and space usage.
```

Expected observations:

- Write-heavy workloads benefit from MemTable buffering and sequential SSTable creation.
- Point reads improve when Bloom filters avoid unnecessary SSTable checks.
- Compaction reduces stale versions and read cost but increases background I/O.
- Universal compaction can reduce write amplification but may increase read or space amplification.
- Leveled compaction usually improves organized reads but rewrites data more often.

### System Behavior

RocksDB is fast when writes can be absorbed into memory and background compaction keeps up. Performance degrades when compaction falls behind because L0 files accumulate, reads check more files, and write stalls may occur to prevent unbounded growth.

## 6. Key Learnings

- RocksDB is optimized for write-heavy key-value workloads, not SQL semantics by itself.
- The WAL protects recent writes, while MemTables absorb them in memory.
- SSTables are immutable, so compaction is required to reclaim space and improve reads.
- Bloom filters reduce read amplification for point lookups.
- LSM-tree design turns random writes into sequential writes, but moves cost into background compaction.
- RocksDB tuning is mostly about balancing read amplification, write amplification, and space amplification.

## References

- RocksDB Wiki: RocksDB Overview - https://github.com/facebook/rocksdb/wiki/RocksDB-Overview
- RocksDB Wiki: Compaction - https://github.com/facebook/rocksdb/wiki/Compaction
- RocksDB Wiki: Write Stalls - https://github.com/facebook/rocksdb/wiki/Write-Stalls
- RocksDB Wiki: RocksDB Tuning Guide - https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide
- RocksDB GitHub Repository - https://github.com/facebook/rocksdb
