# 1. Problem Background

Traditional database systems such as PostgreSQL and MySQL rely heavily on B-Tree indexes for data storage and retrieval. B-Trees provide efficient search performance and support logarithmic-time lookups, inserts, and updates.

However, B-Tree based systems face challenges when operating under modern write-intensive workloads. Every update may require random disk writes, page splits, and frequent modifications to existing storage structures.

As storage systems evolved to handle:

* Large-scale key-value workloads
* Log analytics
* Time-series data
* Distributed databases
* High write-throughput applications

new storage architectures were required.

One of the most influential alternatives is the **Log-Structured Merge Tree (LSM Tree)**.

The LSM Tree was originally proposed to optimize write performance by converting random writes into sequential writes. Instead of modifying data in place, incoming updates are first written to memory and later flushed to disk in batches.

RocksDB, developed by Facebook (now Meta), is one of the most widely used LSM-tree based storage engines. It extends the ideas introduced in LevelDB and is designed for:

* High write throughput
* Efficient SSD utilization
* Large-scale key-value storage
* Embedded database applications
* Distributed storage systems

RocksDB achieves these goals through a combination of:

* Write-Ahead Logging (WAL)
* MemTables
* Immutable MemTables
* SSTables
* Multi-level storage organization
* Bloom Filters
* Compaction algorithms

These mechanisms work together to minimize random writes while maintaining efficient read performance.

This study explores the internal architecture of RocksDB, examines the operation of its LSM-tree storage engine, analyzes its read and write paths, and compares its design philosophy with traditional B-Tree based database systems.

# 2. RocksDB Architecture Overview

RocksDB is an embedded key-value storage engine based on the Log-Structured Merge Tree (LSM Tree).

Unlike traditional B-Tree based databases that update data pages in place, RocksDB converts writes into sequential operations and organizes data into immutable files that are periodically merged through compaction.

A simplified view of the RocksDB architecture is shown below:

```text
                   Client
                      |
                      v
                Write Request
                      |
                      v
               Write-Ahead Log
                      |
                      v
                 MemTable
                      |
                      v
           Immutable MemTable
                      |
                      v
                SSTables (L0)
                      |
                Compaction
                      |
         +------------+------------+
         |            |            |
         v            v            v
        L1           L2           Ln
```

The architecture is designed around two primary goals:

1. Maximize write throughput.
2. Minimize random disk I/O.

To achieve this, RocksDB separates data into multiple stages of storage.

---

## 2.1 Write-Ahead Log (WAL)

The WAL is the first component involved in every write operation.

```text
Write
  |
  v
WAL
```

Before data is stored in memory, RocksDB records the operation inside the Write-Ahead Log.

Benefits:

* Durability
* Crash recovery
* Protection against data loss

If the system crashes before data reaches disk, RocksDB can replay WAL records during recovery.

---

## 2.2 MemTable

The MemTable is an in-memory sorted data structure.

```text
Write
   |
   v
MemTable
```

By default RocksDB uses a Skip List implementation.

Characteristics:

* Sorted key ordering
* Fast insert operations
* Fast lookups
* Memory-resident

Incoming writes are first inserted into the MemTable rather than immediately updating disk files.

This design converts many small random writes into larger sequential operations.

---

## 2.3 Immutable MemTable

A MemTable has limited capacity.

When it becomes full:

```text
MemTable
    |
    v
Immutable MemTable
```

The MemTable becomes read-only.

A new MemTable is created for incoming writes.

Meanwhile, the Immutable MemTable is prepared for flushing to disk.

This mechanism prevents write operations from stopping while data is being persisted.

---

## 2.4 SSTables

When an Immutable MemTable is flushed:

```text
Immutable MemTable
         |
         v
       SSTable
```

an SSTable (Sorted String Table) is created.

Properties:

* Immutable
* Sorted
* Sequentially written
* Optimized for SSDs

SSTables are the fundamental on-disk storage units of RocksDB.

Experimental observation:

```text
000009.sst
000011.sst
```

were generated during the benchmark workload, confirming that MemTable flushes produced persistent SSTable files.

---

## 2.5 Multi-Level Organization

RocksDB organizes SSTables into multiple levels.

```text
L0
 |
 v
L1
 |
 v
L2
 |
 v
L3
 |
 v
Ln
```

Each level contains larger amounts of data than the previous level.

General characteristics:

| Level | Typical Property       |
| ----- | ---------------------- |
| L0    | Newly flushed SSTables |
| L1    | Compacted files        |
| L2    | Larger sorted datasets |
| Ln    | Largest storage region |

This hierarchical organization helps control read and write amplification.

---

## 2.6 Compaction Engine

Over time, multiple SSTables accumulate.

Without maintenance:

```text
Read
  |
Many SSTables
```

would become expensive.

RocksDB therefore performs:

```text
Compaction
```

which merges SSTables and reorganizes data between levels.

Benefits:

* Reduced read amplification
* Improved lookup performance
* Removal of obsolete records
* Better storage efficiency

Compaction is one of the defining characteristics of LSM-tree based systems.

---

## 2.7 Bloom Filters

Searching every SSTable during a lookup would be inefficient.

RocksDB uses Bloom Filters to quickly determine:

```text
Key Exists?
```

or

```text
Key Definitely Not Present
```

Benefits:

* Faster lookups
* Reduced disk access
* Lower read amplification

Bloom Filters allow RocksDB to avoid examining SSTables that cannot contain the requested key.

---

## 2.8 Read and Write Paths

RocksDB separates:

### Write Path

```text
WAL
  |
MemTable
  |
SSTables
```

optimized for high throughput.

---

### Read Path

```text
MemTable
   |
Immutable MemTable
   |
Bloom Filters
   |
SSTables
```

optimized for efficient retrieval.

This separation is one of the key reasons RocksDB achieves very high write performance.

---

## 2.9 Design Philosophy

The central idea behind RocksDB is simple:

```text
Avoid Random Writes
```

Instead of repeatedly modifying existing pages:

```text
Write
  |
Sequential Append
```

operations dominate the storage engine.

The trade-off is that reads become more complex and require additional mechanisms such as Bloom Filters and Compaction.

This represents a deliberate engineering decision that favors write-intensive workloads while maintaining acceptable read performance.

---

## 2.10 Summary

RocksDB is built around the LSM-tree storage model.

Its major components include:

* Write-Ahead Log (WAL)
* MemTables
* Immutable MemTables
* SSTables
* Multi-Level Storage
* Compaction Engine
* Bloom Filters

Together these components allow RocksDB to deliver high write throughput, efficient SSD utilization, and scalable storage performance for modern data-intensive applications.

# 3. LSM Tree Design

The core storage structure used by RocksDB is the **Log-Structured Merge Tree (LSM Tree)**.

The LSM Tree was designed to solve a major limitation of traditional B-Tree based storage engines:

```text id="k2mh7z"
Random Disk Writes
```

B-Trees frequently modify existing pages, which can become expensive on large storage systems.

LSM Trees take a different approach.

Instead of updating data in place:

```text id="l0v3d5"
Write
   |
Sequential Append
```

operations are used whenever possible.

This transforms many small random writes into large sequential writes that are significantly more efficient on modern storage devices.

---

## 3.1 Motivation Behind LSM Trees

Consider a traditional B-Tree.

```text id="vwhz5m"
Insert
   |
Locate Leaf
   |
Modify Page
   |
Write Back
```

Each update may require:

* Page modifications
* Page splits
* Random disk writes

As write rates increase:

```text id="o9y9ku"
More Writes
      |
Higher I/O Cost
```

becomes a bottleneck.

LSM Trees were introduced to minimize these costs.

---

## 3.2 Basic Structure

An LSM Tree consists of:

```text id="6mks34"
MemTable
     |
     v
Immutable MemTable
     |
     v
Level 0
     |
Compaction
     |
Level 1
     |
Level 2
     |
...
     |
Level N
```

Data gradually moves from memory into increasingly larger levels.

This process is managed automatically through compaction.

---

## 3.3 Write Optimization

The primary advantage of an LSM Tree is write performance.

Example:

```sql id="1dwx4v"
PUT(key,value)
```

Execution:

```text id="xq7bmv"
WAL
  |
MemTable
```

No immediate disk update is required.

When the MemTable becomes full:

```text id="ax2ewj"
Flush
  |
SSTable
```

writes occur sequentially.

Advantages:

* High throughput
* SSD-friendly access patterns
* Reduced random writes

---

### Experimental Observation

Observed during benchmarking:

```text id="mxn0rl"
1,000,000 Writes
```

Result:

```text id="xj1xhy"
86,327 ops/sec
11.584 μs/op
```

This demonstrates the effectiveness of the LSM-tree write path.

---

## 3.4 Multi-Level Storage

RocksDB stores SSTables in multiple levels.

Example:

```text id="5xlg2w"
L0
 |
L1
 |
L2
 |
L3
 |
Ln
```

Each level is larger than the previous one.

Typical pattern:

```text id="cbkwm5"
L0 = Small
L1 = Larger
L2 = Larger
Ln = Largest
```

As data moves downward:

* Storage becomes more organized.
* Read efficiency improves.
* Duplicate entries are removed.

---

## 3.5 Level 0 (L0)

Newly flushed SSTables are placed into:

```text id="u6xx7l"
Level 0
```

Characteristics:

* Files may overlap.
* Recently written data resides here.
* Highest write activity.

Example:

```text id="lf3vzs"
SSTable A
SSTable B
SSTable C
```

Keys may exist in multiple files.

This makes reads more expensive.

Compaction addresses this issue.

---

## 3.6 Higher Levels

After compaction:

```text id="wp5w6l"
L0
 |
Compaction
 |
L1
```

Files become:

```text id="ff8qha"
Non-overlapping
Sorted
```

Example:

```text id="z7ozcq"
L1

File A
1 - 100

File B
101 - 200

File C
201 - 300
```

Search operations become more efficient because fewer files need to be examined.

---

## 3.7 Read Amplification

LSM Trees optimize writes but introduce a new challenge:

```text id="gb7dko"
Read Amplification
```

A key may exist in:

```text id="7pkcz6"
MemTable
Immutable MemTable
L0
L1
L2
...
Ln
```

A lookup may need to search multiple structures.

Example:

```text id="byvn5r"
Read Request
      |
Check MemTable
      |
Check L0
      |
Check L1
      |
Check L2
```

This increases read cost.

---

## 3.8 Write Amplification

Compaction introduces another trade-off:

```text id="6xpyw7"
Write Amplification
```

Suppose:

```text id="4hy8va"
L0
  |
Compaction
  |
L1
```

Data is rewritten multiple times as it moves through levels.

Example:

```text id="i2xvzz"
Original Write
      |
Rewrite During Compaction
      |
Rewrite Again
```

The same logical record may be written to storage multiple times.

---

## 3.9 Space Amplification

Temporary duplication during compaction increases storage usage.

Example:

```text id="ry8v3j"
Old SSTable
      +
New SSTable
```

Both may coexist during compaction.

This creates:

```text id="r2cww4"
Space Amplification
```

although usually only temporarily.

---

## 3.10 Experimental Observation

During the benchmark:

```text id="2svxlg"
Raw Size
≈ 110.6 MB
```

Estimated compressed size:

```text id="9pdwzy"
≈ 62.9 MB
```

Observed database size:

```text id="pwg4l2"
≈ 59 MB
```

This demonstrates how:

```text id="6fujrw"
Compression
+
SSTables
```

help reduce storage consumption.

---

## 3.11 Why LSM Trees Work Well on SSDs

Modern SSDs perform very well with sequential writes.

LSM Trees naturally generate:

```text id="njb30k"
Large Sequential Writes
```

through:

```text id="tw6qim"
Flushes
Compactions
```

Benefits:

* Higher throughput
* Better SSD utilization
* Reduced random write overhead

This is one reason RocksDB is widely used in modern distributed storage systems.

---

## 3.12 LSM Trees vs B-Trees

### B-Tree

Optimized for:

```text id="hfr7oq"
Reads
Point Lookups
Range Queries
```

Trade-off:

```text id="z50n3d"
Random Writes
```

---

### LSM Tree

Optimized for:

```text id="m9u6hx"
Writes
Insert-Heavy Workloads
```

Trade-off:

```text id="h23lgp"
More Complex Reads
Compaction Overhead
```

---

## 3.13 Summary

The LSM Tree is the fundamental storage structure used by RocksDB.

Its design transforms random writes into sequential operations by using:

* MemTables
* Immutable MemTables
* SSTables
* Multi-Level Storage
* Compaction

This architecture provides extremely high write throughput and efficient SSD utilization, at the cost of increased read complexity, write amplification during compaction, and additional storage overhead.

The benchmark results demonstrated strong write performance and efficient storage compression, illustrating why LSM Trees have become the preferred design for many modern key-value storage systems.

# 4. Write Path

The write path is the primary reason RocksDB achieves extremely high write throughput.

Unlike B-Tree based systems that frequently modify existing pages, RocksDB converts writes into sequential operations and postpones expensive disk reorganization through compaction.

A simplified write path is shown below:

```text id="q8rmwu"
PUT(key,value)
       |
       v
      WAL
       |
       v
   MemTable
       |
       v
Immutable MemTable
       |
       v
    SSTable (L0)
       |
       v
   Compaction
       |
       v
   L1 → L2 → Ln
```

Each stage contributes to write performance and durability.

---

## 4.1 Step 1 – Client Write Request

Consider a write operation:

```text id="xkvy74"
PUT(User123, Value)
```

The client submits a key-value pair to RocksDB.

At this point the operation exists only in memory and has not yet been persisted.

To guarantee durability, RocksDB first records the operation in the Write-Ahead Log.

---

## 4.2 Step 2 – Write-Ahead Log (WAL)

The WAL is the first persistent component in the write path.

```text id="9qcg0m"
Write
  |
  v
 WAL
```

Before updating memory structures:

```text id="f4kqhr"
Key
Value
Operation
```

are appended to the WAL.

Advantages:

* Crash recovery
* Durability
* Sequential disk writes

If the system crashes:

```text id="0qz5z8"
Restart
   |
Replay WAL
```

allows lost MemTable contents to be reconstructed.

---

### Experimental Observation

During the benchmark:

```text id="o5jl3v"
rocksdb.write.wal = 100000
```

and

```text id="c6l7lz"
rocksdb.wal.bytes ≈ 13.1 MB
```

were observed.

This confirms that every write generated WAL activity before reaching persistent storage structures.

---

## 4.3 Step 3 – Insert Into MemTable

After WAL logging:

```text id="lkgvl4"
Write
   |
MemTable
```

The record is inserted into the active MemTable.

Characteristics:

* In-memory
* Sorted
* Fast insertion
* Typically implemented as a Skip List

Example:

```text id="2h2xjh"
User101
User102
User103
```

Entries remain sorted as new keys arrive.

Because the MemTable resides entirely in memory, inserts are extremely fast.

---

## 4.4 Why MemTables Improve Performance

Without a MemTable:

```text id="ntmwe9"
Write
  |
Disk Update
```

would occur for every operation.

Instead:

```text id="4f55w9"
Write
  |
Memory
```

absorbs incoming updates.

Benefits:

* Reduced disk I/O
* Higher throughput
* Better SSD utilization

This is the foundation of the LSM-tree design.

---

## 4.5 Step 4 – MemTable Becomes Full

A MemTable has limited capacity.

Eventually:

```text id="8cxzn0"
MemTable Full
```

occurs.

At that moment:

```text id="d4uxsx"
Active MemTable
       |
       v
Immutable MemTable
```

The existing MemTable becomes read-only.

A new MemTable is immediately created.

```text id="r4wbdh"
New Writes
      |
New MemTable
```

This prevents write operations from blocking while flushing occurs.

---

## 4.6 Immutable MemTable

The Immutable MemTable acts as a temporary holding area.

```text id="3qzhnf"
Immutable MemTable
         |
         v
      Flush
```

Characteristics:

* Read-only
* Waiting for disk persistence
* Cannot accept new writes

Its only purpose is to prepare data for SSTable creation.

---

## 4.7 Step 5 – Flush to SSTable

The Immutable MemTable is eventually written to disk.

```text id="fqbr7m"
Immutable MemTable
         |
         v
       SSTable
```

Properties:

* Sorted
* Immutable
* Sequentially written

This operation converts memory-resident data into a durable on-disk structure.

---

### Experimental Observation

During the first benchmark:

```text id="0zj1w7"
100,000 Entries
```

Result:

```text id="a7mdk6"
0 SSTables
```

Observation:

The MemTable never filled.

No flush occurred.

---

During the larger benchmark:

```text id="97z9kw"
1,000,000 Entries
```

Result:

```text id="cw7gcz"
000009.sst
000011.sst
```

Observed:

```text id="s94hv8"
2 SSTables Created
```

This confirms the transition:

```text id="w5lnvj"
MemTable
   |
Flush
   |
SSTable
```

within the RocksDB write path.

---

## 4.8 Step 6 – Placement Into Level 0

New SSTables are initially stored in:

```text id="5y1mlv"
Level 0 (L0)
```

Characteristics:

* Recently flushed files
* May contain overlapping key ranges
* Highest write activity

Example:

```text id="j4k2li"
SSTable A
SSTable B
```

Both may contain nearby keys.

As SSTables accumulate, read costs increase.

Compaction becomes necessary.

---

## 4.9 Step 7 – Compaction

Compaction merges SSTables and reorganizes data.

```text id="zcfjlwm"
L0
 |
Compaction
 |
L1
 |
L2
```

Goals:

* Reduce overlap
* Remove obsolete versions
* Improve lookup efficiency
* Reclaim space

Compaction is responsible for much of RocksDB's write amplification.

However it significantly improves read performance.

---

## 4.10 Write Throughput Observation

Benchmark:

```text id="ggstpi"
1,000,000 Writes
```

Result:

```text id="7d8rlp"
86,327 Operations/Second
11.584 μs/op
```

This demonstrates the efficiency of the LSM-tree write path.

Because writes are absorbed by the WAL and MemTable before reaching SSTables, RocksDB achieves very high insertion throughput.

---

## 4.11 Advantages of the Write Path

### Sequential Writes

Most disk activity is append-oriented.

### High Throughput

Memory absorbs bursts of writes efficiently.

### Crash Recovery

WAL protects against failures.

### SSD Optimization

Large sequential operations are favored over random updates.

---

## 4.12 Trade-Offs

### Compaction Overhead

Data may be rewritten multiple times.

### Write Amplification

The same record can move through several levels.

### Temporary Storage Growth

Compaction may require additional disk space.

---

## 4.13 Summary

The RocksDB write path is optimized around the principle of avoiding random writes.

A write operation progresses through:

```text id="g2hxwp"
WAL
  |
MemTable
  |
Immutable MemTable
  |
SSTable
  |
Compaction
```

This architecture enables extremely high write throughput while maintaining durability and efficient SSD utilization.

The benchmark results demonstrated strong insertion performance and confirmed the creation of SSTables after MemTable flushing, illustrating the operation of the LSM-tree write pipeline in practice.

# 5. Read Path

While the LSM Tree architecture significantly improves write performance, it introduces additional complexity for reads.

Unlike B-Tree based systems where data is typically located through a single index traversal, RocksDB may need to search multiple structures before locating a key.

A simplified read path is shown below:

```text
GET(key)
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
L1 → L2 → ... → Ln
```

The goal of the read path is to locate the most recent version of a key while minimizing disk accesses.

---

## 5.1 Why Reads Are More Complex

Consider a write operation:

```text
PUT(User123, Value)
```

Over time the record may move through:

```text
MemTable
   |
Immutable MemTable
   |
L0
   |
L1
   |
L2
```

As a result, the requested key could exist in several locations.

A read operation must determine:

```text
Where Is The Latest Version?
```

This challenge is one of the primary trade-offs of the LSM-tree design.

---

## 5.2 Step 1 – Search Active MemTable

The first lookup occurs in the active MemTable.

```text
GET(key)
    |
Active MemTable
```

Because the MemTable resides entirely in memory:

```text
Memory Lookup
```

is extremely fast.

If the key is found:

```text
Return Value
```

No disk access is required.

---

## 5.3 Step 2 – Search Immutable MemTable

If the key is not found:

```text
Miss
```

RocksDB checks the Immutable MemTable.

```text
Active MemTable
      |
      v
Immutable MemTable
```

This structure may contain recently written records that have not yet been flushed to SSTables.

Again:

```text
Memory Access
```

makes this lookup inexpensive.

---

## 5.4 Step 3 – Consult Bloom Filters

If neither MemTable contains the key:

```text
Check SSTables
```

would normally be required.

However searching every SSTable would be inefficient.

RocksDB therefore uses:

```text
Bloom Filters
```

to eliminate unnecessary file accesses.

---

### What Is a Bloom Filter?

A Bloom Filter is a probabilistic data structure used to test whether a key may exist.

Possible outcomes:

```text
Definitely Not Present
```

or

```text
Possibly Present
```

A Bloom Filter never produces false negatives.

If the filter says:

```text
Not Present
```

the SSTable can be skipped entirely.

---

## 5.5 Bloom Filter Example

Suppose:

```text
SSTable A
```

contains:

```text
User100
User200
User300
```

Query:

```text
GET(User999)
```

Bloom Filter:

```text
Definitely Not Present
```

Result:

```text
Skip SSTable
```

No disk read is necessary.

This significantly reduces read amplification.

---

## 5.6 Step 4 – Search Level 0 SSTables

If Bloom Filters indicate a possible match:

```text
Search L0
```

Level 0 files are checked first.

Characteristics:

* Newest SSTables
* Overlapping key ranges
* Most recent flushed data

Example:

```text
L0
 |
 +-- SSTable A
 |
 +-- SSTable B
```

Because overlap is allowed, multiple files may need to be examined.

---

## 5.7 Step 5 – Search Higher Levels

If the key is not found in L0:

```text
L1
 |
L2
 |
L3
```

are searched.

Unlike L0:

```text
Non-overlapping SSTables
```

exist within each level.

Example:

```text
L1

File A
1 - 100

File B
101 - 200

File C
201 - 300
```

Only one file per level typically needs to be examined.

This improves lookup efficiency.

---

## 5.8 Read Amplification

Because a key may require searching:

```text
MemTable
Immutable MemTable
L0
L1
L2
...
```

RocksDB experiences:

```text
Read Amplification
```

Definition:

```text
Extra Work Per Read
```

compared to simpler storage structures.

Bloom Filters and Compaction are specifically designed to reduce this overhead.

---

## 5.9 Role of Compaction

Compaction improves read performance by:

```text
Reducing SSTables
Removing Obsolete Entries
Organizing Levels
```

Without compaction:

```text
Many SSTables
       |
Slow Reads
```

With compaction:

```text
Fewer SSTables
       |
Faster Reads
```

Thus compaction benefits both storage efficiency and query performance.

---

## 5.10 Experimental Observation

Read benchmark:

```text
100,000 Random Reads
```

Observed result:

```text
1,101,685 Operations/Second
0.907 μs/op
```

This demonstrates that despite the complexity of the LSM-tree read path, RocksDB can achieve extremely high lookup throughput.

The combination of:

* Memory-resident MemTables
* Bloom Filters
* Organized SSTables
* Compaction

allows read performance to remain competitive.

---

## 5.11 Advantages of the Read Path

### Fast Memory Lookups

MemTables are checked before disk structures.

### Bloom Filter Optimization

Many SSTables can be skipped entirely.

### Sorted SSTables

Efficient searches within files.

### Compaction Support

Reduces read amplification over time.

---

## 5.12 Trade-Offs

### Multiple Search Locations

A key may exist in several structures.

### Read Amplification

Additional work compared to direct page lookups.

### Dependency on Compaction

Poor compaction can degrade read performance.

---

## 5.13 Summary

The RocksDB read path is designed to compensate for the complexity introduced by LSM-tree storage.

A lookup progresses through:

```text
MemTable
   |
Immutable MemTable
   |
Bloom Filters
   |
SSTables
```

before returning the requested value.

Although reads are inherently more complex than writes in an LSM-tree system, Bloom Filters, compaction, and level organization allow RocksDB to maintain very high read throughput, as demonstrated by the benchmark results exceeding one million random reads per second.

# 6. Bloom Filters and Compaction

The LSM-tree architecture significantly improves write performance, but it introduces new challenges:

```text
Read Amplification
Write Amplification
Space Amplification
```

RocksDB addresses these challenges using two critical mechanisms:

* Bloom Filters
* Compaction

Together, these components allow RocksDB to maintain high write throughput while preserving efficient read performance and reasonable storage utilization.

---

## 6.1 Bloom Filters

A Bloom Filter is a probabilistic data structure used to determine whether a key may exist in an SSTable.

Instead of opening every SSTable during a lookup:

```text
GET(key)
    |
Bloom Filter
    |
Possible?
```

RocksDB first checks the Bloom Filter.

---

### Possible Outcomes

A Bloom Filter can return:

```text
Definitely Not Present
```

or

```text
Possibly Present
```

A Bloom Filter never produces false negatives.

If it reports:

```text
Not Present
```

the key is guaranteed not to exist in that SSTable.

---

### Why Bloom Filters Matter

Consider:

```text
L0
 |
 +-- SSTable A
 +-- SSTable B
 +-- SSTable C
```

Without Bloom Filters:

```text
Read Request
      |
Open A
Open B
Open C
```

may occur.

With Bloom Filters:

```text
Read Request
      |
Bloom Filter
      |
Skip Unrelated SSTables
```

Only potentially relevant files are searched.

---

## 6.2 Bloom Filter Internals

A Bloom Filter consists of:

```text
Bit Array
     +
Hash Functions
```

Example:

```text
Key
 |
Hash1
Hash2
Hash3
 |
Set Bits
```

During lookup:

```text
Key
 |
Hash Functions
 |
Check Bits
```

If any required bit is missing:

```text
Key Not Present
```

can be determined immediately.

---

## 6.3 False Positives

Bloom Filters are probabilistic.

Possible outcomes:

```text
Correct Negative
Correct Positive
False Positive
```

False positives occur when:

```text
Filter Says Present
```

but the key is actually absent.

However:

```text
False Negatives
```

are impossible.

This makes Bloom Filters extremely useful for storage systems.

---

## 6.4 Bloom Filter Trade-Off

Increasing Bloom Filter size:

```text
More Memory
      |
Fewer False Positives
```

Decreasing Bloom Filter size:

```text
Less Memory
      |
More False Positives
```

Database engineers must balance memory consumption and lookup efficiency.

---

## 6.5 Why Compaction Is Necessary

As writes continue:

```text
MemTable
   |
Flush
   |
SSTable
```

new SSTables accumulate.

Example:

```text
L0

SSTable A
SSTable B
SSTable C
SSTable D
```

Without maintenance:

```text
More Files
      |
More Reads
```

would occur.

Compaction solves this problem.

---

## 6.6 What Is Compaction?

Compaction merges SSTables and reorganizes data across levels.

```text
Before

L0
 |
A
B
C

After

L1
 |
Merged File
```

The resulting files are:

* Sorted
* Larger
* Better organized

This improves lookup efficiency.

---

## 6.7 Minor Compaction

Minor compaction occurs when:

```text
Immutable MemTable
       |
Flush
       |
SSTable
```

A new SSTable is generated.

Example:

```text
MemTable
    |
Flush
    |
L0 SSTable
```

This is the first stage of data persistence.

---

## 6.8 Major Compaction

Major compaction occurs when SSTables are merged between levels.

Example:

```text
L0
 |
A
B
C
 |
Compaction
 |
L1
 |
Merged SSTable
```

Major compaction:

* Removes obsolete versions
* Merges overlapping files
* Improves search efficiency

---

## 6.9 Read Amplification

Read amplification measures how much additional work is required for a read operation.

Example:

```text
GET(key)
```

may require searching:

```text
MemTable
Immutable MemTable
L0
L1
L2
```

instead of a single structure.

Higher read amplification:

```text
More CPU
More Disk Access
```

Compaction reduces this problem.

---

## 6.10 Write Amplification

Compaction introduces write amplification.

Example:

```text
Write Once
     |
Compaction
     |
Rewrite
     |
Compaction
     |
Rewrite Again
```

The same logical record may be written multiple times.

This is one of the major costs of LSM-tree systems.

---

## 6.11 Space Amplification

Temporary duplication occurs during compaction.

Example:

```text
Old SSTable
      +
New SSTable
```

Both may coexist temporarily.

Result:

```text
Extra Storage Usage
```

known as space amplification.

---

## 6.12 Experimental Observation

Initial workload:

```text
100,000 Entries
```

Result:

```text
0 SSTables
```

Observation:

The MemTable never became large enough to trigger a flush.

---

Larger workload:

```text
1,000,000 Entries
```

Result:

```text
2 SSTables
```

Observed files:

```text
000009.sst
000011.sst
```

This demonstrates:

```text
MemTable
     |
Flush
     |
SSTable
```

and the beginning of the compaction lifecycle.

---

## 6.13 Compression Observation

Benchmark output:

```text
Raw Data Size
≈ 110.6 MB
```

Estimated compressed size:

```text
≈ 62.9 MB
```

Actual database size:

```text
≈ 59 MB
```

Compression:

```text
Snappy
```

Observation:

Compression significantly reduced storage requirements and partially mitigated space amplification.

---

## 6.14 Why Compaction Is Worth the Cost

Compaction introduces:

```text
Extra Writes
CPU Usage
Storage Overhead
```

However it provides:

```text
Faster Reads
Reduced Overlap
Lower Read Amplification
Better Space Utilization
```

Without compaction, RocksDB would eventually become inefficient as SSTables accumulate.

---

## 6.15 Summary

Bloom Filters and Compaction are essential components of the RocksDB architecture.

Bloom Filters reduce unnecessary SSTable searches by quickly identifying files that cannot contain a requested key.

Compaction reorganizes SSTables, removes obsolete data, and controls read amplification.

Together they allow RocksDB to balance the competing demands of:

* High write throughput
* Fast lookups
* Efficient storage utilization

making LSM Trees practical for large-scale production workloads.

# 7. PostgreSQL vs RocksDB

PostgreSQL and RocksDB are both highly successful storage systems, but they are designed for different workloads and use fundamentally different storage architectures.

PostgreSQL relies primarily on heap storage and B-Tree indexes, while RocksDB is built around the Log-Structured Merge Tree (LSM Tree).

These design choices lead to different trade-offs in terms of write performance, read performance, storage efficiency, and maintenance overhead.

---

## 7.1 Storage Architecture

### PostgreSQL

Storage model:

```text
Heap Table
     +
B-Tree Indexes
```

Data rows are stored in heap pages.

Indexes contain references to heap tuples.

Example:

```text
Index
  |
Heap Tuple
```

---

### RocksDB

Storage model:

```text
LSM Tree
```

Data progresses through:

```text
WAL
 |
MemTable
 |
SSTables
 |
Levels
```

Records are not updated in place.

Instead:

```text
New Versions
      |
Sequential Writes
```

are generated.

---

## 7.2 Write Performance

### PostgreSQL

Insert or update:

```text
Locate Page
     |
Modify Page
     |
Write Changes
```

Characteristics:

* Good write performance
* Random page modifications
* Page splitting possible

---

### RocksDB

Insert:

```text
WAL
 |
MemTable
```

Characteristics:

* Sequential writes
* Memory buffering
* Very high throughput

Experimental observation:

```text
1,000,000 Writes
```

Result:

```text
86,327 ops/sec
```

This demonstrates the efficiency of the LSM-tree write path.

---

### Winner for Write-Heavy Workloads

```text
RocksDB
```

because writes are converted into sequential operations.

---

## 7.3 Read Performance

### PostgreSQL

Lookup:

```text
B-Tree
   |
Heap Page
```

Advantages:

* Direct access
* Predictable lookup cost
* Efficient range scans

---

### RocksDB

Lookup:

```text
MemTable
 |
Bloom Filters
 |
SSTables
 |
Levels
```

Advantages:

* Fast memory lookups
* Bloom Filter optimization

Trade-off:

```text
Read Amplification
```

may occur.

---

Experimental observation:

```text
1,101,685 reads/sec
```

demonstrates that RocksDB can still achieve extremely high lookup throughput despite the more complex read path.

---

### Winner for Point Lookups

Often:

```text
Tie
```

depending on workload and caching behavior.

---

## 7.4 Range Query Performance

### PostgreSQL

B-Trees naturally support:

```sql
WHERE id
BETWEEN 100
AND 1000
```

Rows are stored in sorted index order.

Advantages:

* Excellent range scans
* Efficient ordered traversal

---

### RocksDB

Range scans require traversal across:

```text
Multiple SSTables
Multiple Levels
```

Compaction helps but complexity remains higher.

---

### Winner for Range Queries

```text
PostgreSQL
```

---

## 7.5 Update Processing

### PostgreSQL

Updates create:

```text
New Tuple Version
```

Old tuples remain until:

```text
VACUUM
```

removes them.

Advantages:

* Simple MVCC model

Trade-off:

```text
Table Bloat
```

---

### RocksDB

Updates create:

```text
New Key Versions
```

Old entries are eventually removed during:

```text
Compaction
```

Advantages:

* Sequential writes

Trade-off:

```text
Write Amplification
```

---

## 7.6 Concurrency Control

### PostgreSQL

Uses:

```text
MVCC
```

based on tuple versioning.

Characteristics:

* Strong concurrency
* Snapshot isolation
* Minimal reader-writer blocking

---

### RocksDB

Primarily a storage engine.

Concurrency is typically managed by the application or higher-level database layer.

Characteristics:

* Focus on storage efficiency
* Less emphasis on relational transaction semantics

---

## 7.7 Recovery Mechanisms

### PostgreSQL

Uses:

```text
WAL
```

for crash recovery.

Workflow:

```text
WAL
 |
Flush
 |
Data Page
```

---

### RocksDB

Uses:

```text
Write-Ahead Log
```

Workflow:

```text
WAL
 |
MemTable
 |
Recovery Replay
```

Both systems rely on write-ahead logging principles.

---

## 7.8 Maintenance Operations

### PostgreSQL

Primary maintenance:

```text
VACUUM
```

Responsibilities:

* Remove dead tuples
* Reclaim storage
* Prevent transaction ID wraparound

---

### RocksDB

Primary maintenance:

```text
Compaction
```

Responsibilities:

* Merge SSTables
* Remove obsolete versions
* Reduce read amplification

---

### Architectural Difference

PostgreSQL:

```text
VACUUM
```

cleans heap storage.

RocksDB:

```text
Compaction
```

reorganizes SSTables.

---

## 7.9 Storage Efficiency

Experimental observation:

```text
Raw Data
≈ 110.6 MB
```

Compressed size:

```text
≈ 62.9 MB
```

Actual database size:

```text
≈ 59 MB
```

Compression:

```text
Snappy
```

RocksDB achieves strong storage efficiency through:

* Compression
* Immutable SSTables
* Compaction

---

PostgreSQL also supports compression, but its storage model is optimized for different workload characteristics.

---

## 7.10 Design Philosophy

### PostgreSQL

Optimized for:

* Relational workloads
* Complex queries
* Transactions
* Range scans
* SQL analytics

---

### RocksDB

Optimized for:

* Key-value storage
* Write-heavy workloads
* SSD utilization
* Embedded systems
* Large-scale distributed databases

---

## 7.11 Summary Comparison

| Feature           | PostgreSQL       | RocksDB                               |
| ----------------- | ---------------- | ------------------------------------- |
| Storage Structure | Heap + B-Tree    | LSM Tree                              |
| Write Strategy    | In-Place Updates | Sequential Writes                     |
| Read Path         | Index + Heap     | MemTable + SSTables                   |
| Range Queries     | Excellent        | Good                                  |
| Write Throughput  | Good             | Excellent                             |
| MVCC              | Native           | Not Primary Focus                     |
| Maintenance       | VACUUM           | Compaction                            |
| Compression       | Supported        | Extensive                             |
| Point Lookups     | Excellent        | Excellent                             |
| SSD Optimization  | Moderate         | High                                  |
| Typical Use Cases | OLTP, Analytics  | Key-Value Stores, Distributed Systems |

Both PostgreSQL and RocksDB are highly optimized systems, but they target different problem domains.

PostgreSQL prioritizes relational capabilities, transactional consistency, and query flexibility, while RocksDB prioritizes write throughput, storage efficiency, and scalable key-value storage.

The choice between them ultimately depends on workload requirements rather than absolute performance, as each architecture represents a different set of engineering trade-offs.

# 8. Experiments and Observations

To better understand the behavior of the RocksDB storage engine, a series of benchmark experiments were conducted using the official `db_bench` utility.

The objectives were:

* Observe write throughput
* Observe read throughput
* Analyze MemTable flushing behavior
* Examine SSTable generation
* Investigate storage utilization
* Understand the practical operation of the LSM-tree architecture

---

## 8.1 Experimental Environment

### Software

```text
RocksDB Version: 8.9.1
```

### Hardware

```text
CPU:
Intel Core i5-10210U

Cores:
8 Logical CPUs

Storage:
SSD
```

### Benchmark Tool

```bash
db_bench
```

provided with RocksDB.

---

## 8.2 Experiment 1 – Small Write Workload

Command:

```bash
db_bench \
  --db=/home/kavya-dhyani/rocksdb_test/db \
  --benchmarks=fillrandom \
  --num=100000 \
  --value_size=100
```

Workload:

```text
100,000 Key-Value Pairs
```

---

### Results

Observed:

```text
58,802 operations/sec
17.006 μs/op
6.5 MB/s
```

---

### Analysis

The benchmark demonstrated high write throughput despite running on a consumer laptop.

More importantly:

```text
0 SSTables
```

were generated.

Observation:

The dataset was small enough to remain entirely within the active MemTable.

As a result:

```text
No Flush
No Compaction
```

occurred.

This confirms that RocksDB delays disk persistence until the MemTable reaches its configured capacity.

---

## 8.3 WAL Activity Observation

Statistics reported:

```text
rocksdb.write.wal = 100000
```

```text
rocksdb.wal.bytes ≈ 13.1 MB
```

Interpretation:

Every write operation was first recorded in the Write-Ahead Log before being inserted into the MemTable.

This confirms the durability guarantees of the write path.

Observed workflow:

```text
Write
  |
WAL
  |
MemTable
```

---

## 8.4 Experiment 2 – Large Write Workload

Command:

```bash
db_bench \
  --db=/home/kavya-dhyani/rocksdb_test/db \
  --benchmarks=fillrandom \
  --num=1000000 \
  --value_size=100
```

Workload:

```text
1,000,000 Key-Value Pairs
```

Estimated raw dataset:

```text
110.6 MB
```

---

### Results

Observed:

```text
86,327 operations/sec
11.584 μs/op
9.6 MB/s
```

---

### Analysis

The larger workload achieved even higher throughput.

Because the dataset exceeded MemTable capacity:

```text
MemTable
     |
Flush
     |
SSTables
```

occurred automatically.

This demonstrates the transition from memory-resident storage to persistent SSTable files.

---

## 8.5 SSTable Observation

Generated SSTables:

```text
000009.sst
000011.sst
```

Count:

```text
2 SSTables
```

Observation:

The presence of SSTables confirms that:

```text
MemTable
     |
Immutable MemTable
     |
Flush
     |
SSTable
```

was successfully executed.

This is one of the defining operations of the LSM-tree architecture.

---

## 8.6 Database Size Analysis

Observed database size:

```text
59 MB
```

Benchmark estimates:

```text
Raw Size
≈ 110.6 MB

Compressed Size
≈ 62.9 MB
```

Compression:

```text
Snappy
```

---

### Analysis

The actual storage consumption closely matched the expected compressed size.

Compression reduced storage requirements by approximately:

```text
110.6 MB
     ↓
59 MB
```

representing a reduction of nearly 47%.

This demonstrates one of the major advantages of immutable SSTable storage.

---

## 8.7 Read Benchmark

Command:

```bash
db_bench \
  --db=/home/kavya-dhyani/rocksdb_test/db \
  --benchmarks=readrandom \
  --num=100000
```

---

### Results

Observed:

```text
1,101,685 operations/sec
0.907 μs/op
```

---

### Analysis

The read benchmark achieved over one million random lookups per second.

This is notable because RocksDB's read path is more complex than its write path.

A lookup may involve:

```text
MemTable
Immutable MemTable
Bloom Filters
SSTables
Multiple Levels
```

Despite this complexity, the combination of:

* Bloom Filters
* SSTable organization
* In-memory structures

allowed extremely high read throughput.

---

## 8.8 LSM Tree Behavior Observed

The experiments demonstrated several key properties of the LSM-tree architecture.

### Small Dataset

```text
Data Remained In Memory
```

Result:

```text
No SSTables
```

---

### Large Dataset

```text
Data Exceeded MemTable Capacity
```

Result:

```text
SSTables Created
```

---

### Persistent Storage

```text
WAL Activity Observed
```

confirming durable write processing.

---

### Compression

```text
Raw Size > Stored Size
```

demonstrating storage efficiency.

---

## 8.9 Overall Findings

The experiments confirmed several important characteristics of RocksDB.

### High Write Throughput

Sequential write processing enabled:

```text
86,327 writes/sec
```

during the larger benchmark.

---

### Efficient Read Performance

The optimized read path achieved:

```text
1.1 million reads/sec
```

during random lookups.

---

### Delayed Persistence

Writes were initially absorbed by MemTables before SSTables were created.

---

### Compression Benefits

Storage consumption was substantially reduced through Snappy compression.

---

### Successful SSTable Generation

The benchmark demonstrated the complete lifecycle:

```text
WAL
 |
MemTable
 |
Immutable MemTable
 |
SSTable
```

which forms the foundation of the RocksDB storage engine.

---

## 8.10 Summary

The experimental results closely matched the theoretical design of RocksDB.

The observations demonstrated:

* WAL-based durability
* Memory-buffered writes
* MemTable flushing
* SSTable creation
* Compression effectiveness
* High read throughput
* High write throughput

Together these results illustrate why RocksDB has become one of the most widely adopted LSM-tree storage engines in modern database systems and distributed storage platforms.

# 9. Key Learnings

This study provided a detailed understanding of the internal architecture of RocksDB and the design principles behind Log-Structured Merge Trees (LSM Trees).

One of the most important insights was that RocksDB prioritizes write performance by transforming random writes into sequential operations. Instead of modifying disk pages directly, incoming updates are first recorded in the Write-Ahead Log and stored in MemTables before eventually being flushed to SSTables. This design significantly reduces write latency and improves throughput.

The experiments demonstrated the effectiveness of this approach. During the benchmark workload, RocksDB achieved more than 86,000 write operations per second while maintaining durability through WAL logging. The creation of SSTables during larger workloads confirmed the operation of the MemTable flush mechanism and the progression of data through the LSM-tree hierarchy.

Another important learning was the role of Bloom Filters and Compaction. Bloom Filters reduce unnecessary SSTable searches and improve read performance, while compaction reorganizes data, removes obsolete records, and controls read amplification. Although compaction introduces write amplification and additional storage overhead, it is essential for maintaining long-term efficiency.

The study also highlighted the trade-offs between LSM Trees and B-Trees. RocksDB achieves exceptional write performance and efficient SSD utilization, but it requires more complex read processing and background maintenance. In contrast, traditional B-Tree based systems such as PostgreSQL provide simpler read paths and superior range-query performance but incur higher costs for write-intensive workloads.

The benchmark results further demonstrated that RocksDB can maintain very high read throughput despite the complexity of the LSM-tree read path. Random lookup performance exceeded one million operations per second through the combined use of MemTables, Bloom Filters, SSTables, and level organization.

Overall, this study illustrates how RocksDB balances durability, write performance, storage efficiency, and scalability through the coordinated operation of WALs, MemTables, SSTables, Bloom Filters, and Compaction. These mechanisms collectively explain why LSM-tree based storage engines have become a preferred choice for many modern key-value stores, distributed databases, and large-scale storage systems.

# 10. References

## Official RocksDB Documentation

1. RocksDB Official Documentation
   https://rocksdb.org/

2. RocksDB GitHub Repository
   https://github.com/facebook/rocksdb

3. RocksDB Wiki
   https://github.com/facebook/rocksdb/wiki

4. RocksDB Architecture Guide
   https://github.com/facebook/rocksdb/wiki/RocksDB-Overview

5. RocksDB MemTable Documentation
   https://github.com/facebook/rocksdb/wiki/Memtable

6. RocksDB SSTable Format Documentation
   https://github.com/facebook/rocksdb/wiki/A-Tutorial-of-RocksDB-SST-formats

7. RocksDB Bloom Filter Documentation
   https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter

8. RocksDB Compaction Documentation
   https://github.com/facebook/rocksdb/wiki/Compaction

9. RocksDB Write-Ahead Log Documentation
   https://github.com/facebook/rocksdb/wiki/Write-Ahead-Log-(WAL)

10. RocksDB Tuning Guide
    https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide

---

## Research Papers

11. Patrick O'Neil, Edward Cheng, Dieter Gawlick, Elizabeth O'Neil
    *The Log-Structured Merge-Tree (LSM-Tree)*
    Acta Informatica, 1996

12. LevelDB Project Documentation
    https://github.com/google/leveldb

13. Designing Data-Intensive Applications
    Martin Kleppmann
    O'Reilly Media

---

## Source Code

14. RocksDB Source Repository
    https://github.com/facebook/rocksdb

15. MemTable Implementation
    db/memtable.*

16. SSTable Implementation
    table/block_based/

17. Compaction Engine Implementation
    db/compaction/

18. WAL Implementation
    db/log_writer.*

---

## Experimental Work

19. RocksDB 8.9.1 Environment Used For This Study

20. Benchmark Experiments Performed Using:

```bash id="h6fptg"
db_bench
```

21. Write Throughput Analysis Performed Using:

```bash id="ef63xl"
fillrandom
```

benchmark workloads.

22. Read Throughput Analysis Performed Using:

```bash id="txw2r3"
readrandom
```

benchmark workloads.

23. SSTable Analysis Performed Using:

```bash id="bzyhkl"
find /home/kavya-dhyani/rocksdb_test/db -name "*.sst"
```

24. Storage Utilization Analysis Performed Using:

```bash id="8mxrpb"
du -sh /home/kavya-dhyani/rocksdb_test/db
```

25. Compression Analysis Based On Benchmark Statistics Generated By RocksDB During Experimental Execution

