#!/usr/bin/env python3
"""run_all.py — reproducible benchmarks: LSM vs B+Tree vs heap.

Measures three dimensions and renders matplotlib charts:
  1. Write throughput  (ops/sec)         — LSM, B+Tree, heap
  2. Point-read latency (microseconds)   — LSM vs B+Tree
  3. Space amplification (disk/logical)  — LSM vs heap (on-disk engines)

Run:
    ./.venv/bin/python benchmarks/run_all.py [N]

Outputs charts (write_throughput.png, read_latency.png, space_amplification.png)
and results.json into the benchmarks/ directory. Deterministic (fixed seed).
"""

from __future__ import annotations

import json
import os
import random
import shutil
import sys
import time

# make `minidb` importable when run as a script from the repo
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb.btree import BPlusTree           # noqa: E402
from minidb.buffer_pool import BufferPool     # noqa: E402
from minidb.disk_manager import DiskManager   # noqa: E402
from minidb.heap import RID, HeapFile         # noqa: E402
from minidb.lsm import LSMTree                # noqa: E402

HERE = os.path.dirname(__file__)
TMP = os.path.join(HERE, "_tmp")
SEED = 1234


def _fresh_tmp() -> str:
    if os.path.exists(TMP):
        shutil.rmtree(TMP)
    os.makedirs(TMP)
    return TMP


def bench_writes(n: int) -> dict[str, float]:
    """Write throughput (ops/sec) for each engine."""
    rng = random.Random(SEED)
    keys = list(range(n))
    rng.shuffle(keys)
    value = b"x" * 64
    out: dict[str, float] = {}

    # LSM
    _fresh_tmp()
    lsm = LSMTree(os.path.join(TMP, "lsm"), memtable_limit=2000, compaction_threshold=4)
    t0 = time.perf_counter()
    for k in keys:
        lsm.put(f"{k:010d}", value)
    lsm.flush()
    out["LSM"] = n / (time.perf_counter() - t0)
    lsm.close()

    # B+Tree (in-memory index key -> RID)
    bt = BPlusTree()
    t0 = time.perf_counter()
    for k in keys:
        bt.insert(k, RID(k, 0))
    out["B+Tree"] = n / (time.perf_counter() - t0)

    # Heap (append-only)
    dm = DiskManager(os.path.join(TMP, "heap.db"))
    heap = HeapFile(BufferPool(dm, num_frames=256))
    t0 = time.perf_counter()
    for k in keys:
        heap.insert(f"{k:010d}".encode() + value)
    out["Heap"] = n / (time.perf_counter() - t0)
    dm.close()
    return out


def bench_reads(n: int, samples: int = 3000) -> dict[str, float]:
    """Average point-read latency (microseconds) for LSM vs B+Tree."""
    rng = random.Random(SEED + 1)
    value = b"y" * 64
    out: dict[str, float] = {}

    _fresh_tmp()
    lsm = LSMTree(os.path.join(TMP, "lsm"), memtable_limit=2000, compaction_threshold=4)
    bt = BPlusTree()
    for k in range(n):
        lsm.put(f"{k:010d}", value)
        bt.insert(k, RID(k, 0))
    lsm.flush()

    probes = [rng.randrange(n) for _ in range(samples)]

    t0 = time.perf_counter()
    for k in probes:
        lsm.get(f"{k:010d}")
    out["LSM"] = (time.perf_counter() - t0) / samples * 1e6

    t0 = time.perf_counter()
    for k in probes:
        bt.search(k)
    out["B+Tree"] = (time.perf_counter() - t0) / samples * 1e6
    lsm.close()
    return out


def bench_space(n: int) -> dict[str, float]:
    """Space amplification = on-disk bytes / live-logical bytes (LSM vs heap).

    Both engines insert N entries then churn: half are overwritten (LSM) / deleted
    (heap). The LSM compacts and reclaims dead versions; the heap's tombstone-only
    deletes leave dead bytes behind (Postgres would need VACUUM). This is the
    space cost the LSM pays cheap writes to avoid.
    """
    value = b"z" * 64
    entry = 10 + len(value)
    out: dict[str, float] = {}

    _fresh_tmp()
    # LSM: insert N, overwrite half (dead versions), then compact to reclaim
    lsm = LSMTree(os.path.join(TMP, "lsm"), memtable_limit=2000, compaction_threshold=4)
    for k in range(n):
        lsm.put(f"{k:010d}", value)
    for k in range(0, n, 2):
        lsm.put(f"{k:010d}", value)        # overwrite -> superseded versions
    lsm.flush()
    lsm.compact()
    lsm_live = n * entry                    # N distinct live keys remain
    out["LSM"] = lsm.stats()["disk_bytes"] / lsm_live
    lsm.close()

    # Heap: insert N, delete half (tombstones, never reclaimed)
    dm = DiskManager(os.path.join(TMP, "heap.db"))
    heap = HeapFile(BufferPool(dm, num_frames=256))
    rids = [heap.insert(f"{k:010d}".encode() + value) for k in range(n)]
    for i in range(0, n, 2):
        heap.delete(rids[i])               # tombstone -> dead bytes stay on disk
    dm.flush()
    heap_live = (n // 2) * entry            # only half the rows are still live
    out["Heap"] = os.path.getsize(os.path.join(TMP, "heap.db")) / heap_live
    dm.close()
    return out


def _bar_chart(path, title, ylabel, data: dict[str, float]):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as e:  # pragma: no cover
        print(f"  (matplotlib unavailable: {e}; skipping chart {os.path.basename(path)})")
        return
    labels = list(data)
    values = [data[k] for k in labels]
    colors = ["#4C72B0", "#DD8452", "#55A868"][: len(labels)]
    fig, ax = plt.subplots(figsize=(5, 4))
    bars = ax.bar(labels, values, color=colors)
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    for b, v in zip(bars, values):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v:,.2f}",
                ha="center", va="bottom", fontsize=9)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    print(f"MiniDB benchmarks (N={n}, seed={SEED})\n")

    writes = bench_writes(n)
    print("Write throughput (ops/sec):")
    for k, v in writes.items():
        print(f"  {k:8s} {v:12,.0f}")

    reads = bench_reads(n)
    print("\nPoint-read latency (microseconds/op):")
    for k, v in reads.items():
        print(f"  {k:8s} {v:10.3f}")

    space = bench_space(n)
    print("\nSpace amplification (on-disk / logical bytes):")
    for k, v in space.items():
        print(f"  {k:8s} {v:8.2f}x")

    _bar_chart(os.path.join(HERE, "write_throughput.png"),
               f"Write throughput (N={n})", "ops/sec", writes)
    _bar_chart(os.path.join(HERE, "read_latency.png"),
               f"Point-read latency (N={n})", "microseconds/op", reads)
    _bar_chart(os.path.join(HERE, "space_amplification.png"),
               f"Space amplification (N={n})", "disk / logical", space)

    results = {"n": n, "seed": SEED, "write_throughput_ops_sec": writes,
               "read_latency_us": reads, "space_amplification_x": space}
    with open(os.path.join(HERE, "results.json"), "w") as f:
        json.dump(results, f, indent=2)
    if os.path.exists(TMP):
        shutil.rmtree(TMP)
    print(f"\nCharts + results.json written to {HERE}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
