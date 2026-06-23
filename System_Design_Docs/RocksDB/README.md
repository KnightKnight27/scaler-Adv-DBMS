# RocksDB Architecture

## 1. Problem Background

Traditional database engines based on B-trees are highly optimized for fast reads. However, when an application needs to ingest massive amounts of data continuously—such as capturing system metrics every millisecond, storing high-volume logs, or updating large caches—a B-tree architecture becomes a massive bottleneck. Updating a B-tree on disk requires random I/O writes, which are fundamentally slow. 

RocksDB was designed to solve this specific problem: achieving extremely high, sustained write throughput. To do this, it abandons the traditional B-tree entirely and embraces an architecture built around a Log-Structured Merge (LSM) Tree, intentionally trading read complexity for write speed.

## 2. Architecture Overview

```text
+---------------+
|  Application  |
+---------------+
    |       | Reads
    | Writes|------------+
    v       v            v
+----------------+  +----------------+
| MemTable (RAM) |  | Bloom Filters  |
+----------------+  +----------------+
    | Flushes            | Quickly Skips
    v                    v
+----------------+  +----------------+
| Immutable      |  | L0 SSTable     |
| MemTable       |  +----------------+
+----------------+       | Merges to
    | Sequential Write   v
    v               +----------------+
+----------------+  | L1 SSTable     |
| L0 SSTable     |  +----------------+
+----------------+       | Merges to
                         v
                    +----------------+
                    | L2 SSTable     |
                    +----------------+
```

RocksDB's architecture revolves entirely around buffering writes in memory and writing them out sequentially to disk.

When a write operation occurs, it doesn't immediately go to the main data files. First, it hits an in-memory buffer called the MemTable. Once this buffer fills up, it becomes immutable, and the data is flushed sequentially to disk into a file called an SSTable (Sorted String Table). 

Over time, as more and more SSTables pile up on the disk, RocksDB runs a background process called Compaction. Compaction merges these smaller tables into larger, organized levels (L0, L1, L2, etc.). 

Because data is spread across the MemTable and various SSTables on disk, the read path is much more complex than a B-tree. To read a value, RocksDB checks the MemTable first, then works its way down through the on-disk levels. To prevent reads from becoming impossibly slow, the architecture relies heavily on Bloom Filters to quickly skip files that definitely don't contain the requested data.

## 3. Internal Design

### The MemTable
The MemTable is an in-memory sorted buffer. When an application issues a `PUT` or `DELETE`, it simply updates this memory structure. Because it's purely in RAM, operations are incredibly fast. When the MemTable hits its size limit (usually around 64MB), it is marked as immutable. A new MemTable takes over incoming writes, while the immutable one is flushed directly to disk in one fast, sequential write.

### SSTables (Sorted String Tables)
Once on disk, the data lives in SSTables. These are highly structured, immutable files. Inside an SSTable, the key-value pairs are compressed and stored in data blocks. It also contains an index block to allow binary searching within the file, and crucially, a Bloom Filter block. Because these files are immutable, they are very easy to cache and never require complex locking mechanisms for readers.

### Compaction
If we just kept flushing MemTables to disk, we'd eventually have thousands of overlapping files, making reads terribly slow. Compaction is the background engine that fixes this. It takes SSTables from a lower level (like L0), merges them with overlapping tables in the next level down (L1), sorts the combined data, resolves deleted or overwritten keys, and writes out fresh, clean SSTables. It's essentially a continuous, background garbage collection and sorting process.

### Bloom Filters
To solve the problem of having to check multiple files during a read, RocksDB attaches a Bloom Filter to each SSTable. A Bloom Filter is a probabilistic data structure that can answer one question: "Is this key in this file?" It might sometimes say "yes" when the answer is no (a false positive), but it will *never* say "no" if the key is actually there. This means if the Bloom Filter says the key isn't in the table, RocksDB can completely skip reading that file from disk.

## 4. Design Trade-Offs

The fundamental trade-off in RocksDB is sacrificing read performance and CPU overhead to gain massive write throughput. By writing data sequentially (flushing MemTables) rather than updating existing blocks in place (like a B-tree), RocksDB avoids random disk I/O. However, this means a single read might have to check multiple levels of the LSM tree to find the most recent version of a key.

To manage this read complexity, RocksDB relies on Leveled Compaction, which introduces another massive trade-off: Write Amplification. Because compaction constantly merges and rewrites tables as they move down the levels, a single piece of data might be rewritten to the disk 7 to 10 times over its lifespan. RocksDB intentionally accepts this heavy write amplification in order to keep the number of files small, which bounds the "Read Amplification" (the number of files we have to check to find a key). 

Another trade-off is Space Amplification. Because `DELETE` operations just write a "tombstone" marker rather than actually deleting the data, and old versions of keys aren't removed until they are compacted, the database takes up significantly more space on disk than the raw data itself. 

## 5. Experiments / Observations

Because RocksDB is a storage engine library rather than a SQL database, we observe its behavior through its internal statistics engine rather than SQL queries. 

By querying the internal properties, we can observe the real-world impact of the Bloom Filters:

```cpp
db.GetProperty("rocksdb.stats")
```
When looking at these stats on a running system, you can clearly see the Bloom Filter hits versus misses. This directly shows how the ~1% memory overhead of the filter successfully eliminates 99% of unnecessary, expensive disk reads when querying for keys that don't exist in certain levels.

We can also observe the Compaction engine at work by monitoring the disk I/O and CPU spikes. When the L0 level hits its threshold, we can watch the system aggressively use CPU to merge the SSTables. If we push the write throughput too high, we can observe a scenario where compaction can't keep up, causing the system to intentionally throttle incoming writes to let the background merging process catch up.

## 6. Key Learnings

Studying RocksDB reveals that you can radically alter the performance characteristics of a system by abandoning traditional data structures like the B-tree. The LSM Tree proves that if you map your operations to sequential disk writes, you can ingest data at speeds that would instantly crush a traditional relational database.

However, I've also learned that these benefits are never free. In RocksDB, high write speed is paid for by CPU-heavy background compactions and high write amplification. The architecture completely relies on auxiliary structures like Bloom Filters just to make read performance tolerable. RocksDB is a perfect example of a specialized tool: it is incredibly powerful for time-series data, logging, or caching layers, but its design trade-offs make it a poor choice for read-heavy, transactional applications where a traditional B-tree would perform much better.
