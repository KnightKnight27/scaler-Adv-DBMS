"""MiniDB benchmark harness.

Runs four experiments and writes results to ``benchmarks/results.json``:

  1. index_vs_scan     point-lookup latency: B+Tree IndexScan vs SeqScan
  2. insert_throughput rows/sec for autocommit vs one batched transaction
  3. buffer_pool       hit ratio / scan time as the pool shrinks below the data
  4. replication       redo records/sec applied to a replica + consistency

Usage:  python benchmarks/run_benchmarks.py [--scale N]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb.engine import Database
from minidb.replication.primary import Primary
from minidb.replication.replica import Replica


def _scratch(name: str) -> str:
    return os.path.join(tempfile.mkdtemp(prefix=f"minidb_bench_{name}_"), name)


def _timeit(fn, repeat: int):
    start = time.perf_counter()
    for _ in range(repeat):
        fn()
    return (time.perf_counter() - start) / repeat


def bench_index_vs_scan(rows: int, queries: int) -> dict:
    db = Database(_scratch("ivs"), pool_size=512)
    c = db.connect()
    # k mirrors id but is NOT indexed, so a lookup on k must scan.
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, k INT, payload TEXT)")
    batch = ",".join(f"({i},{i},'row-{i}')" for i in range(rows))
    c.execute(f"INSERT INTO t VALUES {batch}")

    keys = [(i * 2654435761) % rows for i in range(queries)]
    idx_t = _timeit(lambda: [c.execute(f"SELECT payload FROM t WHERE id = {k}")
                             for k in keys], 1) / queries
    scan_t = _timeit(lambda: [c.execute(f"SELECT payload FROM t WHERE k = {k}")
                              for k in keys], 1) / queries
    db.close()
    return {
        "rows": rows, "queries": queries,
        "index_scan_ms": round(idx_t * 1e3, 4),
        "seq_scan_ms": round(scan_t * 1e3, 4),
        "speedup_x": round(scan_t / idx_t, 1) if idx_t else None,
    }


def bench_insert_throughput(rows: int) -> dict:
    # Autocommit: one transaction (and one log flush) per statement.
    db1 = Database(_scratch("ins_ac"), pool_size=512)
    c1 = db1.connect()
    c1.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    start = time.perf_counter()
    for i in range(rows):
        c1.execute(f"INSERT INTO t VALUES ({i},'v{i}')")
    ac = time.perf_counter() - start
    db1.close()

    # Batched: a single explicit transaction (one commit flush at the end).
    db2 = Database(_scratch("ins_tx"), pool_size=512)
    c2 = db2.connect()
    c2.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    start = time.perf_counter()
    c2.execute("BEGIN")
    for i in range(rows):
        c2.execute(f"INSERT INTO t VALUES ({i},'v{i}')")
    c2.execute("COMMIT")
    tx = time.perf_counter() - start
    db2.close()
    return {
        "rows": rows,
        "autocommit_rows_per_sec": round(rows / ac),
        "batched_txn_rows_per_sec": round(rows / tx),
        "batch_speedup_x": round(ac / tx, 1),
    }


def bench_buffer_pool(rows: int) -> dict:
    db = Database(_scratch("bp_build"), pool_size=512)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, payload TEXT)")
    pad = "x" * 80
    c.execute("INSERT INTO t VALUES " + ",".join(f"({i},'{pad}')" for i in range(rows)))
    total_pages = db.disk.num_pages
    db.close()
    path = db.path_prefix

    out = {"rows": rows, "data_pages": total_pages, "by_pool_size": []}
    for pool in (4, 16, 64, 256):
        d = Database(path, pool_size=pool, recover=False)
        cc = d.connect()
        start = time.perf_counter()
        for _ in range(5):                       # repeated full scans
            cc.execute("SELECT id FROM t")
        elapsed = time.perf_counter() - start
        out["by_pool_size"].append({
            "pool_frames": pool,
            "hit_ratio": round(d.buffer_pool.hit_ratio(), 3),
            "five_scans_ms": round(elapsed * 1e3, 1),
        })
        d.close()
    return out


def bench_replication(rows: int) -> dict:
    primary = Database(_scratch("rep_p"), pool_size=512)
    replica_db = Database(_scratch("rep_r"), pool_size=512)
    c = primary.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    c.execute("INSERT INTO t VALUES " + ",".join(f"({i},'v{i}')" for i in range(rows)))

    prim, rep = Primary(primary), Replica(replica_db)
    start = time.perf_counter()
    lsn = prim.replicate_to(rep, 0)
    elapsed = time.perf_counter() - start

    n_primary = primary.connect().execute("SELECT id FROM t").rowcount
    n_replica = rep.query("SELECT id FROM t").rowcount
    primary.close(); replica_db.close()
    return {
        "rows": rows,
        "log_records_shipped": lsn,
        "apply_seconds": round(elapsed, 4),
        "records_per_sec": round(lsn / elapsed) if elapsed else None,
        "primary_rows": n_primary,
        "replica_rows": n_replica,
        "consistent": n_primary == n_replica,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=int, default=1,
                    help="multiply default dataset sizes")
    args = ap.parse_args()
    s = args.scale

    results = {
        "index_vs_scan": bench_index_vs_scan(rows=5000 * s, queries=2000),
        "insert_throughput": bench_insert_throughput(rows=5000 * s),
        "buffer_pool": bench_buffer_pool(rows=5000 * s),
        "replication": bench_replication(rows=5000 * s),
    }

    out_path = os.path.join(os.path.dirname(__file__), "results.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    print(json.dumps(results, indent=2))
    print(f"\nwrote {out_path}")


if __name__ == "__main__":
    main()
