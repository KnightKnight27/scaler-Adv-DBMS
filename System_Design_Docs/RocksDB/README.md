# RocksDB Architecture: Understanding LSM-Based Storage Systems

## 1. Problem Background

### Motivation Behind RocksDB

As applications began generating enormous amounts of data, storage engines built around traditional B-Tree structures started facing challenges under write-intensive workloads. Systems such as social networks, messaging platforms, and large-scale analytics pipelines continuously generate updates that can overwhelm storage architectures optimized primarily for reads.

RocksDB was developed by Facebook as an enhanced version of Google's LevelDB with the goal of making better use of modern hardware, particularly SSDs and multi-core processors. Instead of focusing on minimizing read latency at all costs, RocksDB prioritizes efficient write handling while still maintaining acceptable read performance.

### Problems RocksDB Attempts to Solve

A major challenge in storage systems is handling a continuous stream of small writes efficiently.

In B-Tree-based databases, updating a small piece of information often requires reading a page from disk, modifying it, and writing the entire page back. When this process occurs millions of times, storage devices experience substantial random I/O overhead.

RocksDB approaches the problem differently. Rather than modifying data in place, it accumulates writes and organizes them through a Log-Structured Merge Tree (LSM Tree). This design converts many random write operations into larger sequential writes, which are significantly more efficient on modern storage devices.

---

## 2. Architecture Overview

### System Architecture

RocksDB is not a complete database management system. It is a storage engine designed to provide fast key-value storage.

Applications interact directly with RocksDB through an embedded library, meaning there is no separate server process and no SQL query engine.

Many distributed databases and data-processing platforms use RocksDB internally as their storage layer.

Examples include:

* Distributed databases
* Stream-processing frameworks
* Large-scale caching systems

### Data Flow During Writes

Whenever a new key-value pair is written, RocksDB processes it through multiple stages.

```text id="v4oqo6"
Application Write
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
        v
SSTable (Level 0)
        |
        v
Compaction Process
```

The key idea is that writes initially occur in memory and are gradually moved to persistent storage through background operations.

---

## 3. Internal Design

### MemTable

The first destination for incoming data is the MemTable.

The MemTable is an in-memory sorted structure that stores recently written entries.

Advantages include:

* Fast insert performance
* Sorted key ordering
* Minimal disk interaction

Because the MemTable resides entirely in memory, write operations can be completed very quickly.

### Write-Ahead Log (WAL)

Before data is placed into the MemTable, RocksDB records the operation inside a Write-Ahead Log.

This ensures durability.

If the system crashes unexpectedly, RocksDB can replay the log and recover data that had not yet reached disk.

### Immutable MemTable

A MemTable cannot continue growing indefinitely.

Once it reaches a configured size limit:

1. It becomes read-only.
2. A new MemTable is created.
3. Background processes prepare the old MemTable for flushing to disk.

This separation allows writes to continue without interruption.

---

### SSTables

Data eventually reaches disk in the form of SSTables (Sorted String Tables).

Characteristics of SSTables:

* Immutable after creation
* Sorted by key
* Optimized for sequential storage

Because files are never modified directly after creation, RocksDB avoids many of the synchronization challenges associated with in-place updates.

---

### Multi-Level Storage Organization

Disk data is organized across multiple levels.

```text id="zj8h2l"
Level 0
   |
Level 1
   |
Level 2
   |
Level 3
   |
   ...
```

#### Level 0

Newly created SSTables are initially placed in Level 0.

Files in this level may contain overlapping key ranges.

#### Lower Levels

As data moves downward through the hierarchy:

* Files become larger.
* Key ranges become non-overlapping.
* Search efficiency improves.

Each level typically stores significantly more data than the previous one.

---

### Compaction

Compaction is the process that keeps the LSM Tree organized.

Without compaction, reads would eventually become inefficient because data would be scattered across many files.

During compaction:

1. SSTables are selected for processing.
2. Records are merged.
3. Obsolete entries are removed.
4. New SSTables are generated.

This process continuously reorganizes the storage structure in the background.

#### Why Compaction Is Necessary

Compaction provides several benefits:

* Removes deleted records
* Eliminates outdated versions
* Improves read efficiency
* Controls storage growth

However, compaction itself consumes CPU, memory, and disk bandwidth.

---

### Read Path

Retrieving a value is more complex than writing one.

To locate a key, RocksDB may search multiple locations:

1. Active MemTable
2. Immutable MemTables
3. Level 0 SSTables
4. Lower storage levels

Because data may exist in several places simultaneously, reads can require additional work compared to traditional B-Tree structures.

This is one of the primary trade-offs of the LSM design.

---

### Bloom Filters

Bloom Filters help reduce unnecessary disk access.

A Bloom Filter is a compact probabilistic structure attached to SSTables.

Before opening an SSTable, RocksDB first checks the associated Bloom Filter.

Possible outcomes:

* Key definitely does not exist.
* Key might exist.

If the filter indicates that a key cannot exist in a file, RocksDB avoids reading that file entirely.

This optimization significantly improves read performance.

---

## 4. Design Trade-Offs

### Strengths of RocksDB

#### Exceptional Write Throughput

Because writes are primarily memory operations followed by sequential logging, RocksDB can process large volumes of updates efficiently.

#### Efficient SSD Usage

Sequential writes are generally more favorable for SSDs than repeated random modifications.

#### Strong Compression Opportunities

Since SSTables are immutable and sorted, compression algorithms often achieve better results compared to fragmented storage layouts.

---

### Limitations of RocksDB

#### Read Amplification

A single logical read may require checking several data structures and storage levels.

As the database grows, this overhead can increase.

#### Write Amplification

Although writes begin efficiently, compaction repeatedly rewrites data as it moves between levels.

Consequently, one logical write may generate multiple physical writes over time.

#### Space Amplification

Old versions and temporary files may remain on disk until compaction eventually removes them.

This causes actual storage usage to exceed the size of the logical dataset.

---

### Balancing the Trade-Offs

A key observation when studying RocksDB is that optimizing one metric often worsens another.

For example:

* Aggressive compaction improves read performance.
* Aggressive compaction increases write amplification.

Similarly:

* Less compaction reduces write costs.
* Less compaction increases read overhead.

Database engineers must tune RocksDB according to workload characteristics rather than pursuing a single universal configuration.

---

## 5. Experiments and Observations

### Comparing Compaction Strategies

RocksDB provides multiple compaction approaches, each targeting different workload requirements.

#### Leveled Compaction

Observations:

* Lower read amplification
* Better space utilization
* Increased write amplification

This strategy is often preferred when read performance is important.

#### Universal Compaction

Observations:

* Reduced write amplification
* Higher storage consumption
* More files examined during reads

This approach is commonly used for highly write-intensive applications.

### Impact of Bloom Filters

Testing read workloads with Bloom Filters enabled showed a noticeable reduction in unnecessary file accesses.

For keys that do not exist, Bloom Filters prevent many disk lookups, reducing latency and improving overall throughput.

---

## 6. Key Learnings

### 1. Write Optimization Drives the Entire Design

Almost every component in RocksDB exists to make writes faster. MemTables, SSTables, and compaction mechanisms are all consequences of this design objective.

### 2. Immutability Simplifies Storage Management

Because SSTables are never modified after creation, RocksDB avoids many challenges associated with page-level updates and locking.

### 3. Fast Writes Create New Challenges

The advantages gained during writes introduce additional complexity for reads and storage management. Compaction is required to keep the system efficient over time.

### 4. Bloom Filters Are Critical for Practical Performance

Without Bloom Filters, the cost of searching across multiple SSTables would significantly impact read latency. They play an essential role in making LSM Trees practical for real-world systems.

### 5. No Storage Engine Optimizes Every Metric

RocksDB highlights an important lesson in database engineering: improving one aspect of performance almost always requires sacrificing another. The challenge is identifying which trade-offs best match the application's workload.

### 6. Architecture Reflects Workload Priorities

Unlike B-Tree-based systems that emphasize balanced performance, RocksDB intentionally favors write-heavy workloads. Its architecture demonstrates how storage engines are often shaped by the specific problems they are expected to solve.
