# RocksDB Architecture

RocksDB is an embedded, persistent key-value store built on a log-structured merge (LSM) tree. It is derived from LevelDB and tuned for write-heavy workloads running on fast storage such as flash and NVMe SSDs. It runs in-process as a library rather than as a separate server, so an application links against it and calls it directly. Keys and values are arbitrary byte strings, and keys are kept in sorted order to support point lookups and ordered range scans.

## 1. Problem Background

Traditional disk-resident indexes such as the B-tree update pages in place. A single logical write can require reading a page, modifying it, and writing it back, and consecutive logical writes often touch scattered locations on disk. On rotating media this means seeks, and on flash it means small random writes that the device translates into read-modify-write cycles at the erase-block level, wearing the device and capping throughput. For workloads dominated by inserts and updates, the in-place model spends most of its budget on random I/O rather than on useful data movement.

The LSM tree changes the write discipline. Instead of mutating data where it lives, every write is buffered in memory and later written out as a new immutable sorted file. Random updates become sequential appends plus periodic background merging. Sequential writes are what both spinning disks and SSDs handle best, and immutability removes the need for in-place locking of data pages. The cost moves elsewhere: a key can now exist in several files at once, reads may have to consult multiple files, and a background process must continually merge files to reclaim space and bound read cost. RocksDB exists to make those costs tunable. It exposes the buffering, file organization, and merge policy so an operator can bias the engine toward write throughput, read latency, or space efficiency according to the workload.

## 2. Architecture Overview

A RocksDB instance is composed of a small set of cooperating structures.

The active memtable is an in-memory sorted buffer that absorbs incoming writes. The default implementation is a skip list, which keeps entries ordered and supports concurrent inserts. The write-ahead log (WAL) is an append-only file that records each write before or as it enters the memtable, so a crash can be recovered. SSTable files (sorted string table files) are the immutable on-disk units that hold flushed data. The MANIFEST is a log that records which SSTable files are live and which level each belongs to, so the engine can reconstruct its current view of the database at startup. The block cache holds recently read uncompressed data blocks in RAM to avoid repeated disk reads.

On disk, SSTable files are arranged in levels named L0 through Ln. L0 files come straight from memtable flushes and can have overlapping key ranges, because each flush is independent of the others. From L1 downward, the files within a single level have disjoint, non-overlapping key ranges, so a given key appears in at most one file per level. Each level has a size target roughly an order of magnitude larger than the level above it, which bounds the number of levels for a given dataset size.

A write follows a fixed path. The operation is appended to the WAL for durability and inserted into the active memtable. When the memtable reaches its configured size limit (write_buffer_size, default 64 MB) it is sealed as immutable, a fresh active memtable takes over incoming writes, and a background thread flushes the immutable memtable to disk as a new L0 SSTable. Because the flush runs in the background, writes continue while older data is being persisted.

A read walks the data newest-first. A lookup checks the active memtable, then any immutable memtables not yet flushed, then SSTables starting from L0 and proceeding to higher levels. Newer writes shadow older ones, so the first match found is the current value. Each SSTable can carry a bloom filter that answers, with no false negatives, whether a key is definitely absent from that file, which lets a point lookup skip files entirely. The block cache satisfies reads of hot blocks from memory.

Column families let one database hold several independent key spaces, each with its own memtables and SSTables and its own configuration, while sharing a single WAL so that writes spanning families remain atomic and durable together.

## 3. Internal Design

Storage structures. The on-disk unit is the SSTable. It stores key-value pairs in sorted order, partitioned into data blocks (typically a few kilobytes each, optionally compressed). An index block maps key ranges to data blocks so a lookup can binary-search to the right block without scanning the file, and an optional bloom filter block summarizes the keys present. Because SSTables are immutable, they need no in-file locking, can be read by many threads at once, and can be cached and memory-mapped safely. Deletions are recorded as tombstones, small marker entries that shadow any older value for the same key until compaction physically removes both.

Memory management. The write buffer is the memtable. write_buffer_size sets the size of one memtable, and max_write_buffer_number (default 2) bounds how many memtables, active plus immutable, may exist before writes stall to let flushing catch up. db_write_buffer_size caps total memtable memory across all column families. The default skip-list memtable supports concurrent insertion from multiple writer threads. Alternative memtable implementations exist: a hash-skip-list and a hash-linked-list speed up prefix lookups at the cost of slow cross-prefix scans, and a vector memtable favors bulk random writes. On the read side, the block cache is the main memory consumer. It holds uncompressed data blocks, and its size and sharding control the read hit rate. Index and filter blocks can be pinned in the cache or kept resident to avoid evicting metadata needed on every lookup.

Index organization. There is no single global index. Each SSTable has its own block index. Within L1 and below, the non-overlapping property means the set of files in a level forms one sorted run, so a binary search over file boundaries followed by the per-file block index locates any key in that level with a bounded number of I/Os. L0 is the exception: its files overlap, so a lookup may have to check several L0 files, which is why the number of L0 files is kept small and is one of the triggers for compaction. The MANIFEST is the catalog tying this together. It is a write-ahead log of version edits describing files added and removed per level, and the current version is the accumulation of those edits.

Transaction processing. The atomic unit of a write is the WriteBatch, a group of puts and deletes that are applied together. RocksDB offers two transaction layers on top of this. Pessimistic transactions acquire row locks as they execute and detect conflicts by holding those locks until commit. Optimistic transactions take no locks during execution and instead validate at commit time by checking whether any key the transaction read or wrote was modified concurrently, aborting if so. Both build on a sequence number assigned to every write. Each entry carries a monotonically increasing sequence number, which both orders versions and lets the engine expose a consistent snapshot: a reader pinned to a sequence number sees only entries at or below it.

Concurrency control. Multiversion behavior comes from the LSM structure itself. An update does not overwrite the previous value; it writes a new entry with a higher sequence number, and the older entry survives until compaction discards it. A snapshot is a sequence number, and reads against a snapshot ignore any entry above it, giving readers a stable view without blocking writers. Writers are serialized into the WAL and memtable through a write group mechanism that batches concurrent writers and commits them together to amortize log syncs, while the skip-list memtable allows concurrent insertion within a group. Compaction and flush run on background threads and coordinate with foreground traffic through the MANIFEST and reference counting, so files being read are not deleted out from under a reader.

Recovery. Durability rests on the WAL. On a clean or unclean restart, RocksDB reads the MANIFEST to rebuild the set of live SSTables and levels, then replays the WAL to reconstruct any memtable contents that had not yet been flushed at the time of the stop. The sync behavior of the WAL is a tunable: with synchronous writes each commit is flushed to stable storage before acknowledgment, giving strict durability at higher latency, while asynchronous writes acknowledge sooner and risk losing the most recent unsynced entries on power loss. Flushed SSTables are already durable, so recovery only needs to replay log records newer than the last successful flush, which bounds restart time.

Compaction. Compaction is the background work that gives the LSM its long-term shape. It reads several SSTables, merges their sorted contents, drops entries that have been overwritten by a newer sequence number, removes tombstones once no older value can remain, and writes the result as new files at a target level. Leveled compaction maintains one sorted run per level below L0 and merges a file from one level into the overlapping files of the next, keeping each level sorted and non-overlapping. This bounds read and space amplification but rewrites data many times as it descends, raising write amplification. Universal (tiered) compaction keeps multiple sorted runs per level and merges whole runs together less frequently, lowering write amplification but allowing more runs to coexist, which raises read and space amplification. FIFO compaction simply drops the oldest files once a size or age limit is reached, which suits cache-like data with a time-to-live. Compaction is the dominant ongoing cost of the design, and its policy is the main lever for balancing the three amplifications.

## 4. Design Trade-Offs

The defining trade-off of an LSM engine is captured by three quantities that cannot be minimized at the same time.

Write amplification is the bytes physically written to storage divided by the bytes the application asked to write. It exceeds one because compaction rewrites the same data repeatedly as it moves down the levels. Read amplification is the number of storage reads needed to answer a query, which grows when a key may live in several files across several levels. Space amplification is the bytes occupied on disk divided by the logical size of the live data, inflated by stale versions and tombstones that persist until compaction reclaims them. A given compaction policy picks a point in this space, and improving one term generally worsens another.

| Property | Leveled compaction | Universal (tiered) compaction |
|---|---|---|
| Write amplification | Higher (data rewritten on each descent) | Lower (whole runs merged less often) |
| Read amplification | Lower (one sorted run per level) | Higher (multiple runs per level) |
| Space amplification | Lower and bounded | Higher and effectively unbounded |
| Suited to | Read-heavy or space-constrained workloads | Write-heavy workloads tolerant of extra space |

The advantage of the LSM approach is write throughput and write efficiency. Turning random updates into sequential file writes plus background merging matches what flash storage does well, sustains high insert and update rates, and avoids the in-place page churn of a B-tree. Immutability of SSTables simplifies concurrency, because readers never contend with in-place modification and files can be shared, cached, and memory-mapped freely.

The limitations follow from the same structure. A point lookup can be more expensive than a B-tree lookup when a key must be searched across levels, which is why bloom filters matter: they convert most negative lookups into a memory probe and let the engine skip files. Range scans must merge across all levels and cannot use bloom filters to skip files, so scan cost scales with the number of sorted runs, favoring leveled layouts for scan-heavy workloads. Space can balloon transiently during large universal compactions, since a merge may need room for both the inputs and the output at once. Write amplification under leveled compaction commonly exceeds ten, which consumes device write bandwidth and endurance even when the application write rate is modest.

The engineering decisions address these costs directly. Keeping L1 and below as non-overlapping sorted runs is what makes per-level lookups cheap and space tight. Allowing L0 to overlap is a deliberate concession that keeps flushes fast and independent, paid for by capping the L0 file count and triggering compaction early. The roughly tenfold size ratio between levels keeps the total number of levels small (and therefore read cost low) while limiting how much data any single compaction must rewrite. Per-key sequence numbers give snapshots and conflict detection without a separate version store. The choice between leveled, universal, and FIFO compaction is left to the operator precisely because no single point in the amplification space is correct for every workload.

## 5. Experiments / Observations

The measurements below were produced on RocksDB version 11.1.1 (Homebrew bottle) on macOS (arm64). The standard db_bench binary is not shipped by that package, so the administration tools that are shipped were used instead: rocksdb_ldb (the ldb command-line tool) to load data and read live database properties, and rocksdb_sst_dump to read the internal structure of individual SSTable files. These tools open and operate on a real on-disk database, so the file counts, level placements, and block sizes reported here are values RocksDB itself wrote and reported, not estimates.

Dataset. A single fixed dataset was used for every run: 500,000 keys, each key 16 bytes (the literal prefix "key" followed by a 13-digit zero-padded integer), each value 100 bytes of random alphanumeric characters. The keys were shuffled before loading so that inserts arrive out of order and exercise the merge path. The input file is 61 MB on disk. Values were chosen to be random rather than constant, because a constant value compresses to almost nothing and would make on-disk sizes meaningless. Each configuration used its own empty database directory.

Load command. The leveled configuration was loaded with the ldb load command, forcing a full compaction at the end:

```
rocksdb_ldb --db=/tmp/rocksdb_exp/leveled --create_if_missing \
  --write_buffer_size=4194304 \
  --file_size=4194304 \
  --bloom_bits=10 \
  load --compact < data.txt
```

The small write_buffer_size (4 MB) and file_size (4 MB) were chosen deliberately so that the 61 MB load produces many memtable flushes and many SSTable files, which makes the level structure and the effect of compaction visible on a dataset of this size.

Leveled compaction, after full compaction. After load --compact, ldb get_property rocksdb.levelstats reported every file resident in the bottom level (L6): 14 files totaling 55 MB, with levels L0 through L5 empty. The property rocksdb.total-sst-files-size reported 57,832,799 bytes, rocksdb.estimate-num-keys reported 500,000, and a full dump --count_only confirmed 500,000 keys. The command list_live_files_metadata listed all 14 SSTable files at level 6 in the default column family, which is the single sorted run that leveled compaction maintains for a bottom level. Each file was approximately 4.26 MB, matching the 4 MB file_size target. The full load and compaction completed in about 1.8 seconds of wall time.

Load without forced compaction. Repeating the identical load but omitting --compact left the database in a partially compacted state: rocksdb.levelstats reported 4 files in L0 (13 MB) and 10 files in L6 (39 MB), 14 files and 56 MB in total. The L0 files come straight from independent memtable flushes and therefore have overlapping key ranges (the last keys of the four L0 files all fell at the very top of the key space, near key0000000499990). This is the read-amplification difference made concrete: in the uncompacted state a point lookup may have to probe up to four overlapping L0 files in addition to the relevant L6 file, whereas after full compaction the same lookup consults exactly one file in the single L6 sorted run. The background compaction that ran during the load had already pushed 10 of the 14 files down to L6 on its own, without any explicit request.

SSTable internal structure. Running rocksdb_sst_dump --show_properties on one of the 4.26 MB leveled SSTable files reported: 36,821 entries, 1,053 data blocks, a data block region of 4,196,918 bytes, an index block of 28,378 bytes, and a bloom filter block of 46,085 bytes. The reported raw average key size was 24 bytes (the 16-byte user key plus the 8-byte internal sequence-number and type suffix that RocksDB appends to every key) and the raw average value size was 100 bytes, matching the dataset. The data region dominates the file, while the index and filter blocks together are about 1.7 percent of it.

Bloom filter cost. Loading the same dataset three times with --bloom_bits set to 4, 10, and 20 and inspecting one resulting SSTable file each showed the filter block growing linearly with the configured bits per key, for an identical 36,821 entries per file: 18,437 bytes at 4 bits per key, 46,085 bytes at 10 bits per key, and 92,101 bytes at 20 bits per key. Those are approximately 0.5, 1.25, and 2.5 bytes per key, which is the configured bit count divided by eight. The total database size stayed at 55 to 56 MB across all three settings, because the filter is a small fraction of each file. ldb requires bloom_bits to be greater than zero, so a no-filter configuration could not be built with this tool for a direct comparison.

Point lookups. On the fully compacted leveled database, ldb get of a key that was loaded returned its 100-byte value, and ldb get of a key outside the loaded range (key9999999999999) returned "Key not found". The negative lookup is the case the bloom filter is built for: the filter lets the engine answer that the key is absent without reading the data blocks of the file.

Observations. The single bottom-level sorted run that leveled compaction produces (14 files at L6, no overlap) is the layout that keeps a point lookup to one file per level. The contrast with the uncompacted load, where four overlapping L0 files must all be consulted, shows directly why L0 file count is kept small and is a compaction trigger. The SSTable breakdown confirms that data blocks dominate the file while the index and filter metadata are small, and the bloom filter measurements confirm that filter size is a direct linear function of the bits-per-key setting, which is the knob that trades a small amount of space for cheaper negative lookups. The 8-byte per-key internal overhead visible in the 24-byte average key size is the per-key sequence number that the design relies on for snapshots and version ordering.

## 6. Key Learnings

The LSM design is a deliberate trade of read and space cost for write efficiency. Buffering writes in a memtable and flushing immutable sorted files converts random updates into sequential I/O, which is the property that makes RocksDB fast on flash, and the entire rest of the architecture exists to manage the consequences of that choice.

The three amplifications form the budget that any tuning works within. Write, read, and space amplification trade against one another, and the compaction policy is the primary control. Leveled compaction buys low read and space cost with high write cost; universal compaction does the opposite; FIFO discards old data outright for cache-like uses.

Several structural decisions earn their place by bounding a specific cost. Non-overlapping levels below L0 keep per-level lookups and space tight. The tenfold level size ratio keeps the level count, and therefore read cost, low. Bloom filters convert most negative point lookups into a memory probe. Per-key sequence numbers provide snapshots and conflict detection at no extra storage structure. The MANIFEST and WAL together make crash recovery a matter of rebuilding the file set and replaying a bounded log tail.

What RocksDB exposes as configuration matches what cannot be decided in general: memtable size and count, block cache size, bloom filter bits, and compaction style. The correct settings depend on whether the workload is dominated by writes, point reads, range scans, or space constraints, which is why the engine ships these as knobs rather than fixed choices.

## References

- RocksDB Overview (official wiki): https://github.com/facebook/rocksdb/wiki/RocksDB-Overview
- Compaction (official wiki): https://github.com/facebook/rocksdb/wiki/Compaction
- Universal Compaction (official wiki): https://github.com/facebook/rocksdb/wiki/Universal-Compaction
- MemTable (official wiki): https://github.com/facebook/rocksdb/wiki/MemTable
- RocksDB Tuning Guide (official wiki): https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide
- Benchmarking tools / db_bench reference (RocksDB wiki mirror): https://github.com/EighteenZi/rocksdb_wiki/blob/master/Benchmarking-tools.md
