#!/usr/bin/env python3
"""SQLite benchmarks for System Design submission."""
import os
import sqlite3
import subprocess
import time
import multiprocessing as mp
from pathlib import Path

WORKDIR = Path(__file__).parent / "experiment_data"
WORKDIR.mkdir(exist_ok=True)


def fresh_db(name: str) -> Path:
    db = WORKDIR / name
    for p in [db, Path(str(db) + "-wal"), Path(str(db) + "-shm")]:
        p.unlink(missing_ok=True)
    return db


def setup_main_db(db: Path) -> None:
    conn = sqlite3.connect(db)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=FULL")
    conn.executescript("""
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            age INTEGER
        );
        CREATE TABLE orders (
            id INTEGER PRIMARY KEY,
            user_id INTEGER NOT NULL,
            amount REAL NOT NULL
        );
        CREATE INDEX idx_users_age ON users(age);
        CREATE INDEX idx_orders_user_id ON orders(user_id);
    """)
    conn.executemany(
        "INSERT INTO users VALUES (?, ?, ?)",
        [(i, f"user_{i}", 18 + (i % 60)) for i in range(1, 100_001)],
    )
    conn.executemany(
        "INSERT INTO orders VALUES (?, ?, ?)",
        [(i, 1 + (i % 100_000), round((i % 5000) + 10.99, 2)) for i in range(1, 50_001)],
    )
    conn.execute("ANALYZE")
    conn.commit()
    conn.close()


def writer(args: tuple[int, str]) -> int:
    proc_id, db_path = args
    busy = 0
    conn = sqlite3.connect(db_path, timeout=0.1)
    for i in range(1, 101):
        try:
            conn.execute("INSERT INTO t VALUES (?, ?)", (proc_id * 1000 + i, f"p{proc_id}_{i}"))
            conn.commit()
        except sqlite3.OperationalError:
            busy += 1
    conn.close()
    return busy


def main():
    results = {}
    print("=== SQLite version ===")
    ver = subprocess.check_output(["sqlite3", "--version"], text=True).strip()
    print(ver)
    results["sqlite_version"] = ver

    db = fresh_db("test.db")
    print("\n=== Setup: 100,000 users + 50,000 orders ===")
    t0 = time.perf_counter()
    setup_main_db(db)
    setup_ms = (time.perf_counter() - t0) * 1000
    print(f"Setup completed in {setup_ms:.0f} ms")
    results["setup_ms"] = setup_ms

    conn = sqlite3.connect(db)

    print("\n=== Experiment 1: 1000 point lookups (warm cache) ===")
    conn.execute("SELECT * FROM users WHERE id = 50000").fetchone()
    t0 = time.perf_counter()
    for _ in range(1000):
        conn.execute("SELECT * FROM users WHERE id = 50000").fetchone()
    lookup_ms = (time.perf_counter() - t0) * 1000
    avg_lookup = lookup_ms / 1000
    print(f"Total: {lookup_ms:.2f} ms | Avg: {avg_lookup:.3f} ms per lookup")
    results["lookup_1000_total_ms"] = lookup_ms
    results["lookup_avg_ms"] = avg_lookup

    print("\n=== Experiment 2: EXPLAIN QUERY PLAN (join) ===")
    plan = conn.execute("""
        EXPLAIN QUERY PLAN
        SELECT u.name, o.amount
        FROM users u
        JOIN orders o ON u.id = o.user_id
        WHERE u.age > 30
    """).fetchall()
    for row in plan:
        print(row)
    results["join_plan"] = [str(r) for r in plan]

    print("\n=== Experiment 3: Join query count + timing ===")
    t0 = time.perf_counter()
    join_count = conn.execute("""
        SELECT COUNT(*)
        FROM users u
        JOIN orders o ON u.id = o.user_id
        WHERE u.age > 30
    """).fetchone()[0]
    join_ms = (time.perf_counter() - t0) * 1000
    print(f"Rows matched: {join_count} | Time: {join_ms:.2f} ms")
    results["join_count"] = join_count
    results["join_ms"] = join_ms

    conn.close()

    print("\n=== Experiment 4: File sizes ===")
    db_size = db.stat().st_size
    wal = Path(str(db) + "-wal")
    wal_size = wal.stat().st_size if wal.exists() else 0
    print(f"test.db: {db_size / 1024 / 1024:.2f} MB")
    if wal.exists():
        print(f"test.db-wal: {wal_size / 1024:.1f} KB")
    results["db_size_mb"] = round(db_size / 1024 / 1024, 2)
    results["wal_size_kb"] = round(wal_size / 1024, 1)

    print("\n=== Experiment 5: Concurrent writes (10 processes x 100 rows) ===")
    cdb = fresh_db("concurrent.db")
    init = sqlite3.connect(cdb)
    init.execute("PRAGMA journal_mode=WAL")
    init.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)")
    init.commit()
    init.close()

    t0 = time.perf_counter()
    with mp.Pool(10) as pool:
        busy_counts = pool.map(writer, [(i, str(cdb)) for i in range(1, 11)])
    concurrent_ms = (time.perf_counter() - t0) * 1000
    rows = sqlite3.connect(cdb).execute("SELECT COUNT(*) FROM t").fetchone()[0]
    total_busy = sum(busy_counts)
    print(f"Time: {concurrent_ms:.0f} ms | Rows: {rows}/1000 | SQLITE_BUSY: {total_busy}")
    results["concurrent_ms"] = concurrent_ms
    results["concurrent_rows"] = rows
    results["concurrent_busy"] = total_busy

    print("\n=== Experiment 6: Durability check ===")
    ddb = fresh_db("durability.db")
    conn = sqlite3.connect(ddb)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=FULL")
    conn.execute("CREATE TABLE d (id INTEGER PRIMARY KEY)")
    conn.execute("BEGIN")
    conn.executemany("INSERT INTO d VALUES (?)", [(i,) for i in range(1, 1001)])
    conn.commit()
    conn.close()
    count = sqlite3.connect(ddb).execute("SELECT COUNT(*) FROM d").fetchone()[0]
    print(f"Committed rows after reopen: {count}")
    results["durability_rows"] = count

    out = WORKDIR / "results.txt"
    with open(out, "w") as f:
        for k, v in results.items():
            f.write(f"{k}={v}\n")
    print(f"\nResults saved to {out}")


if __name__ == "__main__":
    mp.set_start_method("spawn", force=True)
    main()
