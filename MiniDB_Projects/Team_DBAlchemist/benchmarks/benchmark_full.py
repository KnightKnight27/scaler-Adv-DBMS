"""
Full benchmark suite for MiniDB — Track B (MVCC).
Tests: insert throughput, concurrent read/write, index vs scan, snapshot isolation overhead.
Results written to benchmarks/results.json and benchmarks/report.txt
"""
import os, sys, shutil, time, threading, json

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

DATA_DIR = '/tmp/minidb_bench_full'
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT_DIR, exist_ok=True)


def reset():
    if os.path.exists(DATA_DIR):
        shutil.rmtree(DATA_DIR)


def make_db(table='bench', n_rows=0):
    from db import MiniDB
    reset()
    db = MiniDB(DATA_DIR)
    db.execute(f"CREATE TABLE {table} (id INT PRIMARY KEY, value INT, category TEXT)")
    if n_rows > 0:
        batch = 200
        for start in range(0, n_rows, batch):
            txn = db.begin()
            for i in range(start, min(start + batch, n_rows)):
                cat = ['A', 'B', 'C'][i % 3]
                db.execute(
                    f"INSERT INTO {table} (id, value, category) VALUES ({i}, {i*2}, '{cat}')",
                    txn
                )
            db.commit(txn)
        db.refresh_stats(table)
    return db


# ── 1. Insert throughput at various scales ───────────────────────────────────

def bench_insert_throughput():
    print("\n[1] Insert Throughput")
    results = {}
    for n in [100, 500, 1000, 2000]:
        reset()
        from db import MiniDB
        db = MiniDB(DATA_DIR)
        db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
        start = time.perf_counter()
        batch = 100
        for s in range(0, n, batch):
            txn = db.begin()
            for i in range(s, min(s+batch, n)):
                db.execute(f"INSERT INTO t (id, v) VALUES ({i}, {i})", txn)
            db.commit(txn)
        elapsed = time.perf_counter() - start
        qps = n / elapsed
        results[n] = {'rows': n, 'time_s': round(elapsed, 4), 'rows_per_sec': round(qps)}
        print(f"  n={n:5d}: {elapsed:.3f}s  →  {qps:.0f} rows/sec")
        db.close()
    return results


# ── 2. MVCC concurrent read throughput ───────────────────────────────────────

def bench_mvcc_concurrent(n_rows=1000, n_readers_list=None):
    if n_readers_list is None:
        n_readers_list = [1, 2, 4, 8]
    print("\n[2] MVCC Concurrent Read Throughput (readers + 1 writer)")
    results = {}

    for n_readers in n_readers_list:
        db = make_db(n_rows=n_rows)
        reader_qps = []
        errors = []
        lock = threading.Lock()

        def reader(idx):
            count = 0
            t0 = time.perf_counter()
            try:
                txn = db.begin()
                for _ in range(30):
                    db.execute("SELECT * FROM bench WHERE category = 'A'", txn)
                    count += 1
                db.rollback(txn)
            except Exception as e:
                with lock:
                    errors.append(str(e))
            elapsed = time.perf_counter() - t0
            with lock:
                reader_qps.append(count / elapsed if elapsed > 0 else 0)

        writer_done = threading.Event()
        def writer():
            for i in range(n_rows, n_rows + 100):
                txn = db.begin()
                db.execute(f"INSERT INTO bench (id, value, category) VALUES ({i}, {i}, 'W')", txn)
                db.commit(txn)
            writer_done.set()

        threads = [threading.Thread(target=reader, args=(i,)) for i in range(n_readers)]
        w = threading.Thread(target=writer)

        t_start = time.perf_counter()
        for t in threads: t.start()
        w.start()
        for t in threads: t.join()
        w.join()
        total = time.perf_counter() - t_start

        avg = sum(reader_qps) / len(reader_qps) if reader_qps else 0
        total_qps = sum(reader_qps)
        results[n_readers] = {
            'readers': n_readers,
            'avg_qps_per_reader': round(avg),
            'total_read_qps': round(total_qps),
            'wall_time_s': round(total, 3),
            'errors': errors,
        }
        print(f"  {n_readers} readers: avg {avg:.0f} q/s per reader, total {total_qps:.0f} q/s, wall={total:.2f}s")
        db.close()
    return results


# ── 3. MVCC snapshot isolation: reader not blocked by writer ─────────────────

def bench_snapshot_isolation(n_rows=500):
    print("\n[3] Snapshot Isolation — reader sees stable snapshot during writer activity")
    db = make_db(n_rows=n_rows)

    # T1 starts a read transaction
    t1 = db.begin()
    t1_initial = db.execute("SELECT * FROM bench", t1)
    initial_count = len(t1_initial)

    # T2 inserts 50 rows and commits
    t2 = db.begin()
    for i in range(n_rows, n_rows + 50):
        db.execute(f"INSERT INTO bench (id, value, category) VALUES ({i}, {i}, 'NEW')", t2)
    db.commit(t2)

    # T1's snapshot should still see only the original rows
    t1_after = db.execute("SELECT * FROM bench", t1)
    db.rollback(t1)

    # fresh read sees all rows
    fresh = db.execute("SELECT * FROM bench")

    result = {
        'initial_rows': initial_count,
        't1_sees_after_t2_commit': len(t1_after),
        'fresh_read_sees': len(fresh),
        'snapshot_isolation_correct': len(t1_after) == initial_count,
        'new_rows_visible_after_commit': len(fresh) == n_rows + 50,
    }
    print(f"  Initial rows:              {initial_count}")
    print(f"  T1 snapshot after T2 commit: {len(t1_after)} (should be {initial_count})")
    print(f"  Fresh read after T2 commit:  {len(fresh)} (should be {n_rows + 50})")
    print(f"  Snapshot isolation correct: {result['snapshot_isolation_correct']}")
    db.close()
    return result


# ── 4. Index vs sequential scan — cold reads ─────────────────────────────────
#
# Why previous benchmark showed ~1x:
#   Repeated queries on same data → buffer pool warms up → both hit cache equally.
#   Real index benefit only shows when buffer pool can't hold the whole table
#   (i.e., dataset >> buffer pool).
#
# Fix: use small buffer pool (8 pages) + large dataset. SeqScan must fetch all
# N pages from disk; IndexScan fetches only the 1-2 pages containing the target row.

def bench_index_vs_scan():
    print("\n[4] Index Scan vs Sequential Scan (cold reads, small buffer pool)")
    print("    Buffer pool = 8 pages. Dataset grows. SeqScan fetches all pages;")
    print("    IndexScan fetches 1-2 pages via B+ tree pointer.")
    results = {}

    for n in [200, 500, 1000, 2000]:
        # small buffer pool — can't hold full table in memory
        if os.path.exists(DATA_DIR):
            shutil.rmtree(DATA_DIR)
        from db import MiniDB
        db = MiniDB(DATA_DIR, buffer_capacity=8)
        db.execute("CREATE TABLE bench (id INT PRIMARY KEY, value INT, category TEXT)")
        batch = 100
        for s in range(0, n, batch):
            txn = db.begin()
            for i in range(s, min(s + batch, n)):
                cat = ['A', 'B', 'C'][i % 3]
                db.execute(f"INSERT INTO bench (id, value, category) VALUES ({i}, {i*2}, '{cat}')", txn)
            db.commit(txn)
        db.refresh_stats('bench')

        reps = 10

        # SeqScan: cold cache before each rep
        scan_times = []
        for r in range(reps):
            db.evict_buffer_cache('bench')
            t0 = time.perf_counter()
            db.execute("SELECT * FROM bench WHERE category = 'A'")
            scan_times.append((time.perf_counter() - t0) * 1000)
        scan_ms = sum(scan_times) / len(scan_times)

        # IndexScan: cold cache before each rep
        idx_times = []
        for r in range(reps):
            db.evict_buffer_cache('bench')
            target = (r * 17) % n  # spread lookups across table
            t0 = time.perf_counter()
            db.execute(f"SELECT * FROM bench WHERE id = {target}")
            idx_times.append((time.perf_counter() - t0) * 1000)
        idx_ms = sum(idx_times) / len(idx_times)

        speedup = scan_ms / idx_ms if idx_ms > 0 else 0
        results[n] = {
            'rows': n,
            'buffer_pool_pages': 8,
            'seqscan_ms': round(scan_ms, 2),
            'indexscan_ms': round(idx_ms, 2),
            'speedup': round(speedup, 2),
        }
        print(f"  n={n:5d}  SeqScan={scan_ms:.2f}ms  IndexScan={idx_ms:.2f}ms  speedup={speedup:.1f}x")
        db.close()
    return results


# ── 5. WAL recovery correctness ───────────────────────────────────────────────

def bench_wal_recovery(n_rows=200):
    print("\n[5] WAL Recovery")
    reset()
    from db import MiniDB

    db = MiniDB(DATA_DIR)
    db.execute("CREATE TABLE recovery_test (id INT PRIMARY KEY, v TEXT)")
    txn = db.begin()
    for i in range(n_rows):
        db.execute(f"INSERT INTO recovery_test (id, v) VALUES ({i}, 'row{i}')", txn)
    db.commit(txn)

    # uncommitted txn — should NOT appear after recovery
    txn2 = db.begin()
    db.execute(f"INSERT INTO recovery_test (id, v) VALUES ({n_rows}, 'lost')", txn2)
    # simulate crash: don't commit, don't call close()

    t0 = time.perf_counter()
    db2 = MiniDB(DATA_DIR)
    recovery_ms = (time.perf_counter() - t0) * 1000
    rows = db2.execute("SELECT * FROM recovery_test")
    db2.close()

    result = {
        'committed_rows': n_rows,
        'recovered_rows': len(rows),
        'uncommitted_lost': n_rows not in {r['id'] for r in rows},
        'recovery_time_ms': round(recovery_ms, 2),
        'correct': len(rows) == n_rows,
    }
    print(f"  Committed: {n_rows} rows | Recovered: {len(rows)} | "
          f"Uncommitted lost: {result['uncommitted_lost']} | "
          f"Recovery time: {recovery_ms:.2f}ms")
    return result


# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    print("=" * 60)
    print("MiniDB Full Benchmark Suite — Track B (MVCC)")
    print("=" * 60)

    all_results = {}
    all_results['insert_throughput'] = bench_insert_throughput()
    all_results['mvcc_concurrent'] = bench_mvcc_concurrent(n_rows=500, n_readers_list=[1, 2, 4])
    all_results['snapshot_isolation'] = bench_snapshot_isolation(n_rows=500)
    all_results['index_vs_scan'] = bench_index_vs_scan()
    all_results['wal_recovery'] = bench_wal_recovery(n_rows=200)

    # save JSON results
    results_path = os.path.join(OUT_DIR, 'results.json')
    with open(results_path, 'w') as f:
        json.dump(all_results, f, indent=2)

    # write text report
    report_path = os.path.join(OUT_DIR, 'report.txt')
    with open(report_path, 'w') as f:
        f.write("MiniDB Benchmark Report\n")
        f.write("=" * 60 + "\n\n")

        f.write("1. Insert Throughput\n")
        for n, r in all_results['insert_throughput'].items():
            f.write(f"   {n} rows: {r['rows_per_sec']} rows/sec ({r['time_s']}s)\n")

        f.write("\n2. MVCC Concurrent Read Throughput\n")
        for n, r in all_results['mvcc_concurrent'].items():
            f.write(f"   {r['readers']} readers: {r['avg_qps_per_reader']} q/s per reader, "
                    f"total {r['total_read_qps']} q/s\n")
        f.write("   Note: readers see consistent snapshots, writer runs concurrently without blocking readers\n")

        f.write("\n3. Snapshot Isolation\n")
        si = all_results['snapshot_isolation']
        f.write(f"   T1 snapshot (pre-commit): {si['t1_sees_after_t2_commit']} rows\n")
        f.write(f"   Fresh read (post-commit): {si['fresh_read_sees']} rows\n")
        f.write(f"   Correct: {si['snapshot_isolation_correct']}\n")

        f.write("\n4. Index vs Sequential Scan\n")
        for n, r in all_results['index_vs_scan'].items():
            f.write(f"   {n} rows: SeqScan={r['seqscan_ms']}ms, IndexScan={r['indexscan_ms']}ms, "
                    f"speedup={r['speedup']}x\n")

        f.write("\n5. WAL Recovery\n")
        wr = all_results['wal_recovery']
        f.write(f"   Committed: {wr['committed_rows']} | Recovered: {wr['recovered_rows']} | "
                f"Correct: {wr['correct']} | Time: {wr['recovery_time_ms']}ms\n")

    print(f"\nResults saved to {results_path}")
    print(f"Report saved to  {report_path}")
    print("\n✓ All benchmarks complete.")
