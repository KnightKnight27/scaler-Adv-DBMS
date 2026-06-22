# MiniDB Benchmark Report — Track B (MVCC vs 2PL)

## Objective

Quantify the core claim of the Concurrency extension: under write contention,
MVCC readers do not block, whereas 2PL readers do. This is the
"higher read throughput / reduced blocking under contention" requirement of
Track B.

## Experimental Setup

- **Source:** `benchmarks/mvcc_vs_2pl.cpp`
- **Workload:** 200 "hot" rows, each currently write-locked by a single
  uncommitted writer transaction. Then 200,000 short reader transactions, each
  reading one hot row (round-robin over the 200 keys).
- **Metric:** for each scheme, how many reads are *served* vs *blocked*
  (`LOCK_WAIT`), plus wall-clock time for the read workload.
- **Environment:** Apple clang 21, macOS arm64, `-O2`, single thread (a blocked
  lock returns `LOCK_WAIT` deterministically rather than parking an OS thread).

## Results

| Scheme | Reads served | Reads blocked | Blocked % | Read-workload time |
|--------|-------------:|--------------:|----------:|-------------------:|
| MVCC   | 200,000      | 0             | 0.0%      | ~27 ms             |
| 2PL    | 0            | 200,000       | 100.0%    | ~34 ms             |

(Run `./build/bench_mvcc` — exact ms vary by machine; the served/blocked split
is deterministic.)

## Analysis

- **2PL:** a read requires a shared lock, which is incompatible with the
  writer's exclusive lock on the same row, so **every** read of a write-locked
  row blocks. In a real (multi-threaded) system those readers would stall until
  the writer commits, collapsing read throughput exactly when writes are hot.
- **MVCC:** a read is answered from the snapshot taken at `begin()` via the
  visibility rule `xmin ≤ S AND (xmax == 0 OR xmax > S)`. It never inspects the
  lock table, so the in-flight writer is irrelevant and **no read blocks**.

The single rule difference — *a 2PL read takes a lock; an MVCC read does not* —
is the entire source of the improvement, which is why the served/blocked columns
are mirror images.

## Threats to Validity / Notes

- Single-threaded model measures *blocking incidence*, not raw multi-core
  throughput; it is deterministic and isolates the concurrency-control effect.
- MVCC trades this for space: superseded versions accumulate until `gc()` prunes
  those older than the oldest live snapshot.
- The workload is read-dominated under contention, which is the regime MVCC is
  designed for; under a write-only workload the two schemes converge (both
  serialize writers with an exclusive lock).
