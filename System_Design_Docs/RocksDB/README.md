# RocksDB Architecture

**Name:** Harshita Hirawat  
**Roll number:** 24BCS10044

## 1. Problem Background

RocksDB is an embedded key-value storage engine built around a Log-Structured
Merge tree (LSM tree). It is designed for workloads where sustained write
throughput and control over storage behavior matter more than providing a SQL
server.

A B-tree updates pages in their key ranges, which can turn random writes into
random storage I/O. An LSM tree first turns writes into sequential WAL appends
and ordered in-memory updates. It later reorganizes immutable files in the
background. The random-write problem is not removed; it is transformed into
compaction work that can be batched and scheduled.

This makes RocksDB useful as an embedded engine below databases, stream systems,
metadata services, and write-heavy applications. The application still owns
networking, schema, transactions above the engine, and operational policy.

## 2. Architecture Overview

```text
Write
  -> WAL append
  -> mutable MemTable
  -> immutable MemTable
  -> flush -> L0 SSTables
  -> compaction -> L1 -> L2 -> ... -> Ln

Read
  -> mutable MemTable
  -> immutable MemTables
  -> block cache
  -> Bloom/filter check for candidate SSTables
  -> index block -> data block
  -> merge newest visible value / tombstone
```

Main components:

- **WAL:** protects recent MemTable updates from process or machine failure.
- **MemTable:** ordered in-memory structure receiving new writes.
- **Immutable MemTable:** frozen MemTable waiting to be flushed.
- **SSTable:** immutable sorted file with data blocks, indexes, filters, and
  metadata.
- **Block cache:** keeps frequently read blocks in memory.
- **Compaction:** merges sorted files, discards overwritten values/tombstones
  when safe, and moves data through the LSM shape.

## 3. Internal Design

### 3.1 Write path

A normal write is appended to the WAL and inserted into the mutable MemTable.
When the MemTable reaches its limit, it becomes immutable and a new mutable
MemTable accepts writes. A background flush writes the immutable table as an L0
SSTable.

The foreground path is fast because it avoids searching for and rewriting an
existing disk page. The later flush/compaction path is why write amplification
must be measured: the same logical value may be rewritten through several
levels.

### 3.2 SSTables and levels

SSTables never change in place. L0 files may have overlapping key ranges because
each comes directly from a MemTable. In leveled compaction, files in lower levels
are organized into non-overlapping ranges, and each level has a larger capacity.

One key can occur in several files until compaction removes older versions. A
read must respect sequence numbers and tombstones to return the newest visible
state. RocksDB's [basic architecture](https://github.com/facebook/rocksdb/wiki/RocksDB-Basics)
describes MemTables, flushes, SST files, and the read path.

### 3.3 Bloom filters

A Bloom filter answers “this file definitely does not contain the key” or
“this file may contain the key.” False positives are possible; false negatives
are not. Avoiding unnecessary SSTable data-block reads is especially valuable
for missing keys or overlapping L0/universal files.

More bits per key reduce false positives but consume memory/cache capacity and
add filter construction/probing work. Filters improve read amplification; they
do not remove the need for compaction. See RocksDB's
[Bloom-filter documentation](https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter).

### 3.4 Compaction strategies

| Strategy | Main idea | Suitable tendency |
|---|---|---|
| Leveled | Merge data into bounded, mostly non-overlapping levels | Strong point reads and space control; more rewriting |
| Universal | Merge similarly sized sorted runs | Lower write amplification; more overlap and temporary space |
| FIFO | Retain files until a size/age rule removes the oldest | Very high ingestion for disposable/time-series data; weaker general reads |

Compaction is necessary to bound the number of files checked by reads, remove
obsolete versions, reclaim tombstones, and control space. It can become
expensive because it reads and rewrites large sorted runs. RocksDB documents the
available policies under [Compaction](https://github.com/facebook/rocksdb/wiki/Compaction).

### 3.5 Amplification

- **Write amplification:** physical bytes written divided by logical user bytes.
- **Read amplification:** storage/cache blocks examined for a logical lookup.
- **Space amplification:** physical space divided by live logical data size.

These objectives conflict. Aggressive compaction can improve reads and space by
rewriting more data. Deferring compaction improves writes but leaves more files,
duplicates, or stale data for reads and storage.

## 4. Design Trade-Offs

### Why LSM trees fit write-heavy workloads

- WAL and MemTable updates are sequential/in-memory on the foreground path.
- Sorting many writes together makes storage I/O more efficient.
- Immutable files simplify concurrent readers and checksums.
- Compaction can use background bandwidth instead of delaying every write.

### Costs accepted

- Reads may consult multiple structures and files.
- Compaction consumes CPU, read bandwidth, write bandwidth, and temporary space.
- Write stalls occur when flush/compaction falls behind.
- Tombstones and overwritten versions occupy space until compaction reaches them.
- Tuning depends on workload; there is no universally best compaction strategy.

RocksDB exposes many knobs because the correct trade-off for an append-heavy
telemetry store differs from a point-lookup metadata store. Flexibility is an
advantage, but also an operational burden.

## 5. Experiments / Observations

### Benchmark setup

The official Ubuntu 24.04 `rocksdb-tools` package was run in disposable Docker
containers. The same `db_bench` workload was used for every strategy:

```text
benchmarks     = fillrandom, readrandom, stats
writes         = 200,000
random reads   = 100,000
key/value size = 16 / 400 bytes
threads        = 1
compression    = none
write buffer   = 1 MiB
target file    = 1 MiB
```

`fillrandom` overwrites some random keys, so approximately 63.4% of random reads
found a live key in each run. Results are single local runs and should be read as
behavioral evidence, not universal rankings.

The command template was:

```bash
db_bench \
  --db=/tmp/rocks_<strategy> \
  --benchmarks=fillrandom,readrandom,stats \
  --num=200000 --reads=100000 \
  --key_size=16 --value_size=400 --threads=1 \
  --compression_type=none \
  --write_buffer_size=1048576 \
  --target_file_size_base=1048576 \
  --max_bytes_for_level_base=4194304 \
  --compaction_style=<0|1|2> \
  --statistics
```

For FIFO (`compaction_style=2`), the run also used
`--fifo_compaction_max_table_files_size_mb=1024`.

### Measured results

| Metric | Leveled | Universal | FIFO |
|---|---:|---:|---:|
| Random-write throughput | 81,315 ops/s | 106,608 ops/s | 153,087 ops/s |
| Random-read throughput | 115,852 ops/s | 128,946 ops/s | 31,070 ops/s |
| Compaction bytes read | 229,852,485 | 183,169,780 | 80,387,927 |
| Compaction bytes written | 238,192,208 | 172,560,513 | 78,106,240 |
| Directory size | 57,377,659 B | 63,108,686 B | 82,965,258 B |
| Block-cache misses | 123,224 | 113,785 | 558,935 |

The explicit key/value payload was 83,200,000 bytes
(`200,000 * (16 + 400)`). A simple total-write-amplification proxy was
calculated as:

```text
(user bytes + compaction bytes written) / user bytes
```

| Proxy | Leveled | Universal | FIFO |
|---|---:|---:|---:|
| Write amplification | 3.86x | 3.07x | 1.94x |
| Cache misses per successful lookup | 1.94 | 1.80 | 8.81 |

The write proxy treats one payload-sized sequential WAL write plus the reported
SST/compaction writes as physical work. It deliberately excludes filesystem and
device-level effects, so it is useful for comparing these runs rather than as a
universal hardware write-amplification value. The second metric is a practical
read-amplification proxy, not a formal device I/O count, because RocksDB's
cumulative cache counters include metadata/block activity around the benchmark.

The observed 63.4% live-key hit rate estimates approximately 52,748,800 bytes
of live key/value payload (`0.634 * 200,000 * 416`). Dividing directory size by
that common estimate gives a comparable space-amplification proxy:

| Proxy | Leveled | Universal | FIFO |
|---|---:|---:|---:|
| Space amplification | 1.09x | 1.20x | 1.57x |

This estimate excludes application metadata and uses the sampled read hit rate,
so its value is the relative comparison: leveled used the most rewriting to
keep the tightest on-disk representation, while FIFO retained the most space.

### Interpretation

- **Leveled:** smallest directory and fast reads, but the most compaction bytes.
  Its organization paid write cost to reduce overlap and space.
- **Universal:** best random-read throughput in this run and lower compaction
  write volume than leveled, but used about 10% more directory space.
- **FIFO:** fastest writes and lowest compaction write volume, but random reads
  were roughly four times slower than leveled/universal and cache misses rose
  sharply. It also occupied the most space because files were retained below
  the configured FIFO size limit.

This is the trade-off predicted by the designs: minimizing rewrite work leaves
more files/overlap for the read path. FIFO is not “better” because it won the
write column; it is suitable only when retention/disposable-data semantics fit.

The benchmark command structure follows RocksDB's official
[benchmarking tools](https://github.com/facebook/rocksdb/wiki/Benchmarking-tools).

## 6. Key Learnings

- LSM trees move work from foreground random writes into background compaction.
- Compaction is both cleanup and structural maintenance; disabling work in one
  place moves cost to reads or space.
- Bloom filters reduce unnecessary file reads, especially for absent keys, but
  do not solve overlap by themselves.
- The measured FIFO result demonstrates why throughput must be evaluated with
  read and space amplification, not in isolation.
- RocksDB is a toolkit of trade-offs. Workload shape and retention semantics
  should choose the compaction policy.

## Sources Consulted

- [RocksDB basics](https://github.com/facebook/rocksdb/wiki/RocksDB-Basics)
- [RocksDB compaction](https://github.com/facebook/rocksdb/wiki/Compaction)
- [RocksDB Bloom filters](https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter)
- [RocksDB benchmarking tools](https://github.com/facebook/rocksdb/wiki/Benchmarking-tools)
