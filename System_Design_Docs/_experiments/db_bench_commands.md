# RocksDB `db_bench` Commands

`db_bench` is the canonical RocksDB benchmark tool. The Homebrew `rocksdb` formula used on this machine (11.1.1) ships the library and headers but **not** the `db_bench` binary, so the commands below are for a machine where RocksDB has been built from source (the build includes `db_bench` in its output directory).

> **What was actually run here:** because `db_bench` wasn't available, the labs were measured with [`rocks_demo.cpp`](rocks_demo.cpp) — a small program that links directly against the installed `librocksdb` and reports RocksDB's own statistics. The captured numbers are in [`rocksdb_experiments.txt`](rocksdb_experiments.txt). The `db_bench` commands below map onto that benchmark: fillrandom → Experiment A, readrandom → Experiment B, leveled vs universal → Experiment C.

## Build Check

```bash
which db_bench
db_bench --help
```

If `db_bench` is not installed, build RocksDB and use the binary from the build output directory.

## Write-Heavy Workload

```bash
db_bench \
  --benchmarks=fillrandom,stats \
  --num=1000000 \
  --value_size=1024 \
  --statistics=true \
  --db=/tmp/rocksdb-bench
```

Record:

- Operations per second
- Average and tail latency
- Flush count
- Compaction time
- Write amplification
- Final database size

## Read-Heavy Workload

```bash
db_bench \
  --benchmarks=readrandom,stats \
  --num=1000000 \
  --reads=200000 \
  --use_existing_db=true \
  --statistics=true \
  --db=/tmp/rocksdb-bench
```

Record:

- Read operations per second
- Block cache hit rate
- Bloom filter positive and useful counts
- Files consulted per point lookup, if available in stats

## Leveled Versus Universal Compaction

```bash
db_bench \
  --benchmarks=fillrandom,readrandom,stats \
  --num=1000000 \
  --value_size=1024 \
  --statistics=true \
  --compaction_style=level \
  --db=/tmp/rocksdb-level
```

```bash
db_bench \
  --benchmarks=fillrandom,readrandom,stats \
  --num=1000000 \
  --value_size=1024 \
  --statistics=true \
  --compaction_style=universal \
  --db=/tmp/rocksdb-universal
```

Compare:

- Write amplification
- Read amplification
- Space amplification
- Compaction CPU and I/O time
- Final database directory size
