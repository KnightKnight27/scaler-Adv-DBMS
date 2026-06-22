from __future__ import annotations

import csv
import shutil
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.engine import MiniDBEngine
from minidb.transactions import DeadlockError


def timed(operation):
    start = time.perf_counter()
    result = operation()
    return time.perf_counter() - start, result


def main() -> None:
    runtime_dir = ROOT / "benchmarks" / "runtime"
    if runtime_dir.exists():
        shutil.rmtree(runtime_dir)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    engine = MiniDBEngine(runtime_dir)
    engine.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")
    engine.execute("CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, amount INT);")

    record_count = 100
    insert_duration, _ = timed(lambda: bulk_insert(engine, record_count))
    lookup_duration, lookup_rows = timed(
        lambda: engine.execute("SELECT * FROM users WHERE id = 50;")
    )
    scan_duration, scan_rows = timed(lambda: engine.execute("SELECT * FROM users;"))
    join_duration, join_rows = timed(
        lambda: engine.execute(
            "SELECT * FROM users JOIN orders ON users.id = orders.user_id;"
        )
    )
    concurrent_duration, concurrent_summary = timed(run_concurrent_workload)

    csv_path = ROOT / "benchmarks" / "benchmark_data.csv"
    rows = [
        ["benchmark", "duration_seconds", "rows_or_events"],
        ["insert_100_records", f"{insert_duration:.6f}", record_count],
        ["point_lookup_btree", f"{lookup_duration:.6f}", len(lookup_rows)],
        ["table_scan", f"{scan_duration:.6f}", len(scan_rows)],
        ["nested_loop_join", f"{join_duration:.6f}", len(join_rows)],
        ["2pl_concurrent_workload", f"{concurrent_duration:.6f}", concurrent_summary],
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerows(rows)

    report_path = ROOT / "benchmarks" / "benchmark_results.md"
    report_lines = [
        "# Baseline Benchmark Results",
        "",
        "| Benchmark | Duration (s) | Rows/Events |",
        "| --- | ---: | ---: |",
    ]
    for row in rows[1:]:
        report_lines.append(f"| {row[0]} | {row[1]} | {row[2]} |")
    report_lines.extend(
        [
            "",
            "Notes:",
            "- Insert benchmark loads 100 users and matching orders to seed the rest of the workload.",
            "- Point lookups use the automatically created primary-key B+ Tree.",
            "- The concurrent workload measures strict 2PL lock acquisition and release under two threads.",
        ]
    )
    report_path.write_text("\n".join(report_lines), encoding="utf-8")
    print(f"Wrote {csv_path}")
    print(f"Wrote {report_path}")


def bulk_insert(engine: MiniDBEngine, count: int) -> None:
    for value in range(1, count + 1):
        engine.execute(f"INSERT INTO users VALUES ({value}, 'user_{value}', {20 + (value % 30)});")
        engine.execute(f"INSERT INTO orders VALUES ({value}, {value}, {100 + value});")


def run_concurrent_workload() -> str:
    runtime_root = ROOT / "benchmarks" / "runtime"
    runtime_root.mkdir(parents=True, exist_ok=True)
    temp_path = runtime_root / "concurrent_2pl"
    if temp_path.exists():
        shutil.rmtree(temp_path, ignore_errors=True)
    temp_path.mkdir(parents=True, exist_ok=True)
    try:
        engine = MiniDBEngine(temp_path)
        engine.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);")
        engine.execute("INSERT INTO accounts VALUES (1, 'A', 100);")
        engine.execute("INSERT INTO accounts VALUES (2, 'B', 200);")
        txn_manager = engine.transaction_manager
        results: list[str] = []

        def worker(name: str, first: str, second: str) -> None:
            txn_id = txn_manager.begin()
            try:
                txn_manager.before_write(txn_id, first)
                time.sleep(0.02)
                txn_manager.before_read(txn_id, second)
                txn_manager.commit(txn_id)
                results.append(f"{name}:committed")
            except Exception:
                results.append(f"{name}:aborted")

        thread_one = threading.Thread(
            target=worker, args=("t1", "accounts:pk:1", "accounts:pk:2")
        )
        thread_two = threading.Thread(
            target=worker, args=("t2", "accounts:pk:2", "accounts:pk:1")
        )
        thread_one.start()
        thread_two.start()
        thread_one.join(timeout=2)
        thread_two.join(timeout=2)
        return ",".join(sorted(results))
    finally:
        shutil.rmtree(temp_path, ignore_errors=True)


if __name__ == "__main__":
    main()
