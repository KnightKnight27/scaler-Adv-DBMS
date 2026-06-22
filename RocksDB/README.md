### RocksDB Architecture — An LSM-Tree Based Storage Engine

RocksDB is an embedded persistent key-value database that relies on the **Log-Structured Merge Tree (LSM-tree)** design. Originally derived from Google's LevelDB and later enhanced by Facebook, it was redesigned to take advantage of SSDs and flash storage while supporting highly concurrent, write-intensive workloads. The core motivation behind RocksDB is to optimize storage systems for modern hardware by favoring sequential I/O over random disk updates.

---

## 1. Motivation

### Why traditional update-in-place structures struggle

Historically, databases have commonly used **B-trees** and **B+ trees** for indexing. In these structures, updating a key requires locating the page that contains it, modifying that page, and writing the page back to disk. This provides efficient lookups—typically requiring only `O(log n)` page accesses—but can become costly for heavy write workloads.

The issue comes from storage behavior. Although inserts may arrive sequentially, their positions in a sorted B-tree are scattered across different pages. As a result, the storage device receives many random writes. On hard drives, each random write incurs a costly seek operation. SSDs eliminate seeks but still handle random writes less efficiently due to block erase and rewrite operations inside the flash translation layer, increasing write amplification and device wear.

Thus, there is a mismatch between what storage devices prefer—large sequential writes—and what update-in-place structures generate—small random modifications.

### The LSM-tree approach

LSM-trees address this mismatch by avoiding in-place updates entirely. Instead, they:

1. Collect incoming writes in memory.
2. Periodically flush accumulated data to disk as a large sorted file.
3. Merge these files in the background through compaction.

Rather than performing many random updates, the system converts them into large sequential writes. Updates and deletions are recorded as new versions, while obsolete records are removed later during compaction.

The concept originated in the 1996 LSM-tree paper by O'Neil et al. It later influenced Google's Bigtable architecture, inspired LevelDB, and ultimately evolved into RocksDB, which adds production-oriented features such as configurable compaction policies, column families, extensive statistics, and improved concurrency.

### Comparing B-trees and LSM-trees

| Aspect              | B-tree                | LSM-tree                |
| ------------------- | --------------------- | ----------------------- |
| Update strategy     | Modify data in place  | Append and merge        |
| Primary strength    | Fast reads            | Fast writes             |
| Write amplification | Moderate              | Higher                  |
| Read amplification  | Low                   | Higher                  |
| Space amplification | Low                   | Higher until compaction |
| Write concurrency   | Page-level contention | Mostly append-based     |

In summary, B-trees favor read efficiency, whereas LSM-trees prioritize write throughput.

---

## 2. High-Level Architecture

RocksDB moves data through a pipeline that starts in memory and gradually migrates it into larger storage levels on disk.

The major components include:

### Write-Ahead Log (WAL)

Every write is first recorded in an append-only log on disk. The WAL exists solely for durability. If the process crashes before data reaches disk files, RocksDB can rebuild its state by replaying the log.

### MemTable

The MemTable is the active in-memory write buffer. By default, it is implemented as a skip list, maintaining keys in sorted order and supporting logarithmic-time insertions.

### Immutable MemTable

When a MemTable becomes full, it is frozen and marked immutable. New writes immediately switch to a fresh MemTable while the frozen one waits to be flushed.

### SSTables

Data eventually reaches disk as **Sorted String Tables (SSTables)**. These files contain sorted key-value pairs and never change after creation.

### Storage Levels

Disk files are organized into levels labeled L0 through Ln. Each level is significantly larger than the one above it, typically by a factor of ten.

### Bloom Filters

Each SSTable includes a Bloom filter that quickly determines whether a key definitely does not exist in that file, reducing unnecessary disk accesses.

### Compaction

Compaction continuously merges SSTables, removes obsolete records, and maintains the level structure.

---

## 3. Internal Operation

### Write Path

A write operation such as `Put`, `Delete`, or `Merge` follows two parallel steps:

1. Append the operation to the WAL.
2. Insert the entry into the active MemTable.

Neither step requires random disk updates. WAL writes are sequential, and MemTable operations occur entirely in memory.

Once the MemTable reaches its configured size limit, it becomes immutable and a new active MemTable is created. A background thread then flushes the immutable MemTable into a sorted SSTable at Level 0.

Because the MemTable already maintains sorted order, flushing is essentially one large sequential write.

If data arrives faster than flushing and compaction can process it, RocksDB applies write throttling or stalls to prevent uncontrolled growth of pending work.

---

### SSTables and Immutability

An SSTable consists of:

* Data blocks containing sorted key-value records
* Bloom filter blocks
* Index blocks mapping keys to data locations
* Metadata and footer information

The crucial property of SSTables is immutability. Once written, they are never modified.

This design simplifies concurrency because readers can access files without locking, caches never require invalidation due to updates, and background operations become straightforward sequential writes.

Updates are represented by writing newer versions of keys. Deletions are stored as **tombstones**, which indicate that a key should be treated as deleted. Actual removal happens later during compaction.

---

### Level Organization

#### Level 0

Level 0 receives newly flushed SSTables directly from memory. Since these files are created independently, their key ranges may overlap.

As a result, reads must potentially inspect every Level-0 file.

#### Levels 1 and Below

Starting from Level 1, files within the same level have non-overlapping key ranges. Therefore, a lookup only needs to identify the single file that could contain the target key.

This organization significantly reduces lookup cost.

---

### Compaction Styles

#### Leveled Compaction

Leveled compaction maintains strict non-overlapping levels. When a level becomes too large, files are merged into the next level.

Advantages:

* Lower read amplification
* Lower space amplification

Disadvantage:

* Higher write amplification

#### Universal (Tiered) Compaction

Universal compaction delays merges and allows multiple overlapping runs to coexist.

Advantages:

* Lower write amplification
* Higher write throughput

Disadvantages:

* Higher read amplification
* Higher storage overhead

---

### Read Path

To retrieve a key, RocksDB searches from newest data to oldest:

1. Active MemTable
2. Immutable MemTables
3. Level 0 files
4. Levels 1 through N

The first matching version is returned because newer records always override older ones.

For Level 0, every file may need to be checked due to overlapping key ranges. For deeper levels, only one candidate file per level is examined.

---

### Bloom Filters

Bloom filters are critical for efficient reads.

When querying an SSTable:

* A negative Bloom filter result guarantees the key is absent.
* A positive result only means the key may exist.

False positives are possible, but false negatives are not.

With approximately 10 bits per key, Bloom filters achieve around a 1% false-positive rate. This dramatically reduces unnecessary disk reads, especially for lookups of absent keys.

---

### Compaction

Compaction merges multiple SSTables into larger ones while removing obsolete records and tombstones.

Its responsibilities include:

* Limiting read amplification
* Recovering storage occupied by old versions
* Preserving the level structure

However, compaction is expensive because it repeatedly rewrites data as it moves through levels. A single logical byte may be rewritten many times during its lifetime, resulting in substantial write amplification.

Compaction also consumes CPU, memory, cache capacity, and disk bandwidth, making it one of the most important factors affecting RocksDB performance.

---

## 4. Design Trade-offs

### The RUM Conjecture

The RUM conjecture states that a storage system cannot simultaneously optimize:

* Read cost
* Update cost
* Memory/space overhead

Improving two inevitably worsens the third.

In RocksDB this appears as three forms of amplification:

| Type                | Meaning                                   |
| ------------------- | ----------------------------------------- |
| Write amplification | Physical bytes written vs user bytes      |
| Read amplification  | Number of blocks/files examined per read  |
| Space amplification | Disk space consumed relative to live data |

Leveled compaction reduces read and space amplification while increasing write amplification.

Universal compaction reduces write amplification while increasing read and space amplification.

---

### When LSM-Trees Excel

LSM-trees are particularly effective for:

* High-ingest workloads
* Logging systems
* Time-series databases
* SSD-based storage
* Applications with frequent writes

Their append-oriented design matches the strengths of modern flash storage and avoids page-level contention.

---

### When B-Trees Are Better

B-trees remain attractive for:

* Read-heavy applications
* Frequent point lookups
* Range-scan workloads
* Scenarios where write amplification must remain low

A single tree traversal is often cheaper than searching across multiple LSM levels.

---

### Tombstones and Space Growth

Deletions are not immediately reclaimed. Instead, tombstones persist until compaction encounters and removes all older copies of the corresponding key.

Consequently, delete-heavy workloads may temporarily consume significant extra space and incur additional scan overhead.

---

### Write Stalls

If write generation exceeds the system's ability to flush and compact data, pending work accumulates.

RocksDB responds with back-pressure mechanisms:

* Slowing writes when compaction debt grows.
* Eventually pausing writes if necessary.

Although stalls increase latency, they prevent runaway read amplification and uncontrolled space growth.

---

## Key Takeaways

* The main advantage of RocksDB comes from transforming random updates into sequential writes.
* Immutability enables efficient caching, lock-free reads, and simple background processing.
* Bloom filters are essential for keeping read costs manageable.
* Compaction is the central mechanism that controls performance, storage efficiency, and amplification trade-offs.
* Choosing between leveled and universal compaction is fundamentally a decision about which amplification costs are acceptable.
* There is no universally superior storage engine; the right choice depends entirely on workload characteristics and performance priorities.

