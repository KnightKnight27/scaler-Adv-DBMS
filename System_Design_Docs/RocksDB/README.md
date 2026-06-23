# RocksDB Architecture

## 1. Problem Background

So after studying PostgreSQL and InnoDB, both of which use B-Trees for storage, I started looking at RocksDB and it's a completely different philosophy.

B-Tree databases are great for reads — you can find any key in O(log n) time. But writes? Not so much. Every write is potentially a random disk I/O: find the right page, modify it, maybe split it if it's full, update parent nodes, etc. For workloads where you're ingesting data at really high rates (think event logging, time-series data, metrics collection), this random write pattern becomes the bottleneck.

RocksDB was originally forked from Google's LevelDB and then heavily optimized by Facebook (Meta) for SSDs. It uses a **Log-Structured Merge-Tree (LSM-Tree)** instead of a B-Tree. The core insight is simple: instead of doing random writes to a tree on disk, buffer writes in memory and flush them sequentially. Random writes become sequential writes, which are way faster on both HDDs and SSDs.

It's an embeddable key-value store (not a full SQL database), and it's used as the storage engine behind a ton of systems — CockroachDB, TiKV, Kafka Streams, and even MySQL (via MyRocks).

## 2. Architecture Overview

The architecture revolves around the LSM-Tree. Data flows from memory to disk in a structured way:

- **WAL**: Every write first goes to a write-ahead log on disk (for durability, same idea as PostgreSQL/InnoDB)
- **MemTable**: An in-memory sorted data structure (usually a skip list) that holds recent writes
- **Immutable MemTable**: When a MemTable fills up, it's frozen (made immutable) and a fresh MemTable takes its place
- **SSTables (Sorted String Tables)**: The immutable MemTable gets flushed to disk as an SSTable — a sorted, immutable file of key-value pairs
- **Levels (L0 → Ln)**: SSTables are organized into levels. Compaction moves data from lower levels to higher levels, merging and sorting along the way.

```
  Write arrives
      │
      ├──► WAL (append to disk, for crash recovery)
      │
      └──► MemTable (insert into skip list in memory)
              │
              │ (MemTable full)
              ▼
         Immutable MemTable
              │
              │ (flush to disk)
              ▼
         L0 SSTables (may have overlapping key ranges)
              │
              │ (compaction)
              ▼
         L1 SSTables (non-overlapping key ranges within each level)
              │
              ▼
         L2, L3, ... Ln
```

## 3. Internal Design

### Write Path

This is where RocksDB really shines. The write path is dead simple:

1. Write comes in
2. Append it to the WAL (sequential disk write — fast)
3. Insert it into the MemTable (in-memory — fast)
4. Return success to the caller

That's it. Notice what's missing: no random disk I/O to find and update a B-Tree page. No page splits. No index updates. The actual data doesn't hit disk until the MemTable gets flushed, and even that flush is a sequential write of a sorted file. This is why LSM-trees can handle massive write throughput.

### Read Path

Reading is where things get more complicated (and this is the trade-off):

1. Check the active **MemTable**
2. Check the **Immutable MemTable(s)** (there might be more than one waiting to be flushed)
3. Check **L0 SSTables** (these can have overlapping key ranges, so you might need to check all of them)
4. Check L1, L2, ... Ln SSTables (within each level, key ranges don't overlap, so binary search works)

In the worst case, a read might have to check the MemTable plus multiple SSTables across multiple levels. This is **read amplification** — the price you pay for write-optimized storage.

To mitigate this, RocksDB uses **Bloom Filters** on SSTables. A Bloom filter is a probabilistic data structure that can tell you "this key is definitely NOT in this file" or "this key MIGHT be in this file." It has false positives but no false negatives. So before actually reading an SSTable from disk, RocksDB checks the Bloom filter — if it says "not here," skip the file entirely. This dramatically cuts down on unnecessary disk reads.

### Compaction — The Necessary Evil

As data flows into L0, the number of SSTables grows. Without intervention, reads would get slower and slower because you'd have to check more and more files. Also, old versions of updated keys and deleted keys (which are just "tombstone" markers) are still sitting around taking up space.

**Compaction** is the background process that fixes this:
- Pick SSTables from level N
- Merge them with overlapping SSTables in level N+1
- Produce new sorted SSTables in level N+1
- Discard the old files

This process removes tombstones, merges duplicate keys (keeping only the latest version), and maintains the level structure so reads stay fast.

The problem? Compaction is expensive. It involves reading multiple SSTables, decompressing them, merge-sorting the data, compressing the output, and writing new files. If the write rate is so high that compaction can't keep up, you get "compaction debt" and the system can stall. This is a real operational concern for write-heavy deployments.

There are different compaction strategies too:
- **Leveled compaction**: Better for reads (each level has non-overlapping key ranges), but higher write amplification
- **Universal (tiered) compaction**: Lower write amplification, but potentially worse read performance and more space amplification

## 4. Design Trade-Offs

### The RUM Conjecture

This was one of the most interesting things I learned. There's this idea called the **RUM conjecture** (Read, Update, Memory) that basically says you can't optimize all three of these at the same time:

- **Read amplification**: How many disk reads does a point lookup require?
- **Write amplification**: How many times is the data written to disk over its lifetime? (Once at flush, then rewritten during each level of compaction)
- **Space amplification**: How much extra disk space does the database use beyond the logical dataset? (Due to tombstones, old versions, duplicate keys waiting for compaction)

LSM-trees optimize for write amplification (at the cost of read and sometimes space). B-Trees optimize for read amplification (at the cost of write). You always pay somewhere.

### Advantages
- Writes are incredibly fast because they're all sequential
- SSTables compress really well because the data is sorted and immutable
- Good for SSDs because sequential writes reduce wear leveling overhead

### Disadvantages
- Reads can be slow in the worst case (checking multiple levels)
- Write amplification from compaction — the same data gets rewritten multiple times as it moves down the levels. I've seen numbers like 10-30x write amplification for leveled compaction, which means if you write 1GB of data, the actual disk I/O could be 10-30GB
- Space amplification is also an issue — you need extra disk space to hold old data while compaction is in progress
- Compaction can cause latency spikes (especially if it kicks in during peak traffic)

### Answering the Key Questions

**Why are LSM trees preferred in write-heavy workloads?**
Because they completely avoid the random I/O pattern that kills B-Tree write performance. Every write is just an append to a log and an in-memory insert. No page splits, no random seeks. For workloads like event logging, metrics ingestion, or message queues where you're writing way more than you're reading, this is a massive win.

**Why can compaction become expensive?**
Because it's doing heavy I/O (reading and writing potentially gigabytes of data) and heavy CPU work (decompression, merge-sort, compression). If your write rate is faster than compaction can process, SSTables pile up in L0, reads slow down, and eventually the system might throttle writes to let compaction catch up. It's the Achilles heel of LSM-tree systems.

**How do Bloom Filters improve read performance?**
By letting you skip SSTables that definitely don't contain the key you're looking for. Without Bloom filters, a point lookup might need to read from every SSTable at every level. With Bloom filters (and a typical false positive rate of ~1%), you can skip most of those reads. For point lookups, this makes a huge difference. For range queries though, Bloom filters don't help much.

## 5. Experiments / Observations

Based on what I've read about RocksDB's `db_bench` tool and various benchmark reports:

**Write throughput**: With default settings, RocksDB can sustain hundreds of thousands of key-value writes per second on an SSD. The bottleneck is usually compaction speed and how fast the WAL can sync to disk, not the write path itself.

**Space amplification in action**: Right after a burst of updates (say, overwriting a million keys), the database on disk is noticeably larger than the logical dataset because old versions of those keys still exist in SSTables. After a full compaction, the size drops significantly as old versions are merged away.

**Compaction strategy matters a lot**: Switching from leveled compaction to universal compaction can dramatically change the behavior:
- Leveled: More consistent read latency, but higher write amplification (data is rewritten more often)
- Universal: Lower write amplification, but the database can temporarily use much more disk space, and read latency can be less predictable

The choice between strategies really depends on your workload. There's no one-size-fits-all answer.

## 6. Key Learnings

- **LSM-trees invert the B-Tree trade-off.** B-Trees make writes expensive so reads can be fast. LSM-trees make reads more expensive so writes can be fast. Understanding this trade-off helped me see why different storage engines exist — it's not that one is "better," it's that they're optimized for different access patterns.

- **Deletes are writes in disguise.** In RocksDB, deleting a key doesn't immediately remove anything. It writes a tombstone marker. The actual data survives until compaction merges it away. This was surprising to me at first but makes sense — you can't go find and remove data from immutable SSTables, so you have to record the deletion and clean up later.

- **Compaction is the real engineering challenge.** The write and read paths are conceptually simple. The hard part is compaction — when to trigger it, how to schedule it so it doesn't interfere with foreground operations, how to pick which SSTables to compact, how to balance write amplification vs space amplification. Most of the complexity in RocksDB's codebase is related to compaction.

- **Hardware awareness matters.** RocksDB's sequential write pattern was specifically designed for flash storage. Random writes on SSDs cause write amplification at the flash translation layer, reducing SSD lifespan. By writing sequentially, RocksDB is kinder to SSDs than B-Tree engines that do random page writes. This is a nice example of software design being influenced by hardware characteristics.
