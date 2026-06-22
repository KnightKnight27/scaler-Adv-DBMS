# RocksDB Architecture

## 1. Problem Background
RocksDB is an embedded key-value store from Facebook, based on Google's LevelDB. It was made for write-heavy workloads on fast storage like SSDs, where a normal B-tree's in-place updates cause too many random writes. The answer is the LSM-tree (log-structured merge tree). Instead of updating in place, RocksDB turns writes into sequential appends and sorts them out later in the background. Writes get cheap up front, at the cost of more work on reads and during compaction.

## 2. Architecture Overview
```
  write
    |
  WAL (durability)     MemTable (RAM, sorted)
    |                       | full
    +-----------------------+ flush
                            v
                    Immutable MemTable
                            v flush to disk
                  L0 [sst][sst][sst]
                            | compaction
                  L1 [ sst ][ sst ]
                  L2 [  sst sst sst ]
                  ...
                  Ln (largest, oldest)
```
A write goes to the WAL and the in-memory MemTable. When the MemTable fills it becomes immutable and is flushed to disk as an SSTable in L0. Compaction merges SSTables down the levels.

## 3. Internal Design
The MemTable is a sorted in-memory structure (often a skip list) for recent writes, backed by the WAL for crash safety. When full it's frozen (immutable) and flushed as a sorted SSTable in L0. Lower levels L1..Ln hold older, larger, non-overlapping SSTables. Reads check the MemTable, then L0, then deeper levels, using Bloom filters to skip files that definitely don't hold the key. Compaction is the background job that merges SSTables, drops deleted or overwritten keys, and pushes data deeper.

## 4. Design Trade-Offs
LSM trees make writes fast and sequential, which is great for write-heavy work and easy on SSDs. But that speed is borrowed: reads may check several places, and compaction runs in the background using CPU and disk. This shows up as three "amplifications". Write amp: data gets rewritten as it moves down levels. Read amp: one read may touch many files. Space amp: old versions and tombstones take extra space until compaction cleans them. Tuning compaction is picking which one you pay more of.

## 5. Experiments / Observations
With `db_bench` the trade-offs are clear. A `fillrandom` write test shows very high write throughput because everything just appends to MemTable and WAL. The stats then show write amplification climbing as compaction rewrites data into deeper levels. Switching compaction style changes the numbers: leveled lowers space amp but raises write amp, universal does the opposite. Read tests with and without Bloom filters show how much they cut wasted SSTable reads. You can't minimize all three amps at once.

## Suggested Questions

**Why are LSM trees preferred in write-heavy workloads?**
Because they turn random writes into sequential ones. A new write just goes into the in-memory MemTable and appends to the WAL, both cheap, and the expensive sorting and merging is deferred to background compaction. There's no in-place update forcing a random disk seek per write like a B-tree. Sequential writes are far faster on SSDs and cause less wear. So when the workload is mostly inserts and updates, an LSM tree absorbs them quickly and smooths the heavy work out over time.

**Why can compaction become expensive?**
Compaction reads existing SSTables, merges them, drops deleted or overwritten keys, and writes fresh SSTables deeper down. The same data gets read and rewritten several times as it sinks from L0 to Ln, which is the write-amplification cost. All that uses CPU and disk bandwidth that could serve queries, so heavy compaction can cause latency spikes. If writes come faster than compaction keeps up, L0 files pile up and reads slow too. So it's necessary but it competes with foreground work.

**How do Bloom filters improve read performance?**
A read might otherwise check many SSTables across levels, most of which don't even hold the key, wasting disk reads. A Bloom filter is a small in-memory probabilistic structure per SSTable that cheaply answers "is this key definitely not here?". If it says no, RocksDB skips that file without touching disk. It can have false positives (says maybe when it's absent) but never false negatives, so it's safe. This cuts read amplification a lot, especially for keys that don't exist.

## 6. Key Learnings
What stuck with me is the "three amplifications" idea: write, read, and space amp are like a budget where pushing one down pushes another up, and compaction tuning is how you spend it. LSM trees aren't magic, they're a bet that cheap writes now are worth more background work later, which pays off on write-heavy SSD systems. Bloom filters were the clever bit, a little memory saving tons of disk reads. It reframed storage for me as "pay on write" (B-tree) vs "pay later in compaction" (LSM).
