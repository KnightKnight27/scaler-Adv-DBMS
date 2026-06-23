## Problem Background

RocksDB is designed for high-performance key-value storage where write throughput is extremely important.

While studying it, I understood that traditional databases like B-Tree based systems optimize reads, but RocksDB takes a different approach by optimizing writes using an LSM (Log Structured Merge Tree) design.

This makes it suitable for systems like logging, time-series data, and event ingestion pipelines.

## High-Level Architecture

RocksDB is built on the LSM tree model and consists of:

- MemTable (in-memory write buffer)
- WAL (Write Ahead Log for durability)
- SSTables (immutable sorted files on disk)
- Compaction engine
- Multiple levels of storage (L0 to Ln)
- Bloom filters for fast reads

Write flow:

Client → MemTable → WAL → Flush → SSTable → Compaction → Levels

## Write Path (MemTable → WAL → SSTable)

When a write happens in RocksDB:

1. Data is first written to WAL (for durability)
2. Then inserted into MemTable (in-memory sorted structure)
3. When MemTable becomes full, it is flushed to disk as an SSTable

Key insight:
Writes are fast because they are sequential and happen in memory first, not directly on disk.

## SSTables and Disk Layout

SSTables are immutable sorted files.

Each SSTable contains:
- Sorted key-value pairs
- Index block
- Bloom filter
- Metadata

Once created, SSTables are never modified. Instead, new versions are created during compaction.

This immutability simplifies storage but increases the need for compaction.

## Read Path

When reading a key:

1. Check MemTable first
2. Then check SSTables from L0 to deeper levels
3. Use Bloom filters to skip unnecessary files

If Bloom filter says "key not present", RocksDB avoids disk read entirely.

This reduces read amplification significantly.

## Bloom Filters

Bloom filters are used to quickly check whether a key might exist in an SSTable.

- If Bloom filter returns "NO" → key is definitely not present
- If it returns "YES" → key might be present (false positive possible)

This avoids unnecessary disk reads and improves performance in large datasets.

## Compaction (Core Concept)

Compaction is the process of merging multiple SSTables into fewer SSTables.

Reasons for compaction:
- Remove duplicate keys
- Delete overwritten values
- Improve read performance
- Reduce storage usage

Compaction works like a merge-sort process where sorted files are merged together.

## Levels (L0 → Lmax)

RocksDB organizes SSTables into levels:

- L0: contains overlapping SSTables
- L1+: contains non-overlapping SSTables

As data moves down levels:
- Files become larger
- Overlap decreases
- Read performance improves

Compaction moves data from upper levels to lower levels.

## Write / Read / Space Amplification

RocksDB has trade-offs:

- Write amplification: same data written multiple times during compaction
- Read amplification: multiple SSTables may be checked for a key
- Space amplification: old data may exist until compaction removes it

LSM trees optimize writes at the cost of background compaction overhead.

## Design Trade-offs

RocksDB is optimized for write-heavy workloads.

Advantages:
- Very fast writes (append + memory buffer)
- High ingestion throughput
- Good scalability

Disadvantages:
- Compaction is expensive
- Read performance depends on number of SSTables
- More complex internal system compared to B-Tree databases

## Experiments / Observations

From understanding RocksDB behavior:

- Writes remain fast even under heavy load due to MemTable buffering
- Reads improve significantly when Bloom filters are enabled
- Performance drops if compaction cannot keep up with writes (L0 buildup)

This shows that background compaction is critical for system stability.

## Key Learnings

RocksDB helped me understand how modern databases trade read complexity for write performance.

Key insights:
- LSM trees optimize ingestion speed
- SSTables are immutable, making storage simpler
- Bloom filters reduce unnecessary disk access
- Compaction is essential but expensive

Overall, RocksDB is designed for systems where write throughput matters more than immediate read optimization.