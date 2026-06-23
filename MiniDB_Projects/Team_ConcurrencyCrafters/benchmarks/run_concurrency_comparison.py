from __future__ import annotations

import csv
import math
import shutil
import statistics
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.engine import MiniDBEngine
from minidb.types import TransactionMode


@dataclass(frozen=True)
class Workload:
    name: str
    readers_per_writer: int
    writers_per_round: int
    rounds: int
    hold_seconds: float
    hot_keys: tuple[int, ...]
    description: str


WORKLOADS = [
    Workload(
        name="read_heavy",
        readers_per_writer=9,
        writers_per_round=1,
        rounds=5,
        hold_seconds=0.020,
        hot_keys=(1, 2, 3),
        description="90% reads, 10% writes across three warm keys.",
    ),
    Workload(
        name="mixed",
        readers_per_writer=7,
        writers_per_round=3,
        rounds=4,
        hold_seconds=0.020,
        hot_keys=(1, 2, 3, 4),
        description="70% reads, 30% writes with moderate key reuse.",
    ),
    Workload(
        name="hot_key_contention",
        readers_per_writer=8,
        writers_per_round=2,
        rounds=5,
        hold_seconds=0.030,
        hot_keys=(1,),
        description="Hot-key contention where readers and writers collide on the same key.",
    ),
    Workload(
        name="write_heavy",
        readers_per_writer=5,
        writers_per_round=5,
        rounds=4,
        hold_seconds=0.020,
        hot_keys=(1, 2),
        description="Optional write-heavy mix with equal read and write transaction counts.",
    ),
]


def percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(percent * len(ordered)) - 1))
    return ordered[index]


def avg_ms(values: list[float]) -> float:
    if not values:
        return 0.0
    return statistics.mean(values) * 1000.0


def p95_ms(values: list[float]) -> float:
    return percentile(values, 0.95) * 1000.0


def run_workload(mode: TransactionMode, workload: Workload) -> dict[str, object]:
    runtime_dir = ROOT / "benchmarks" / "runtime" / f"{workload.name}_{mode.value.lower()}"
    shutil.rmtree(runtime_dir, ignore_errors=True)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    engine = MiniDBEngine(runtime_dir)
    engine.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);")
    for account_id in range(1, 21):
        engine.execute(
            f"INSERT INTO accounts VALUES ({account_id}, 'owner_{account_id}', {1000 + account_id});"
        )
    engine.execute(f"SET MODE {mode.value};")

    read_latencies: list[float] = []
    write_latencies: list[float] = []
    committed_txns = 0
    aborted_txns = 0
    blocked_reads = 0
    wait_time_seconds = 0.0
    metrics_lock = threading.Lock()
    total_ops = 0

    benchmark_start = time.perf_counter()
    for round_index in range(workload.rounds):
        for writer_index in range(workload.writers_per_round):
            key = workload.hot_keys[writer_index % len(workload.hot_keys)]
            writer_txn = engine.begin_transaction()
            writer_start = time.perf_counter()
            engine.execute(f"DELETE FROM accounts WHERE id = {key};", txn_id=writer_txn)
            writer_commit_time = 0.0

            reader_threads: list[threading.Thread] = []

            def reader_task(reader_key: int) -> None:
                nonlocal committed_txns, aborted_txns, blocked_reads, wait_time_seconds, total_ops
                reader_txn = engine.begin_transaction()
                start = time.perf_counter()
                try:
                    engine.execute(
                        f"SELECT * FROM accounts WHERE id = {reader_key};",
                        txn_id=reader_txn,
                    )
                    engine.commit_transaction(reader_txn)
                    elapsed = time.perf_counter() - start
                    with metrics_lock:
                        read_latencies.append(elapsed)
                        committed_txns += 1
                        total_ops += 1
                        if writer_commit_time and (start + elapsed) > writer_commit_time:
                            blocked_reads += 1
                            wait_time_seconds += max(0.0, (start + elapsed) - writer_commit_time)
                except Exception:
                    aborted_txns += 1
                    engine.rollback_transaction(reader_txn)

            for reader_index in range(workload.readers_per_writer):
                reader_key = workload.hot_keys[reader_index % len(workload.hot_keys)]
                thread = threading.Thread(target=reader_task, args=(reader_key,))
                reader_threads.append(thread)
                thread.start()

            time.sleep(workload.hold_seconds)
            new_balance = 1000 + (round_index * 100) + writer_index
            engine.execute(
                f"INSERT INTO accounts VALUES ({key}, 'owner_{key}', {new_balance});",
                txn_id=writer_txn,
            )
            engine.commit_transaction(writer_txn)
            writer_commit_time = time.perf_counter()
            write_latencies.append(time.perf_counter() - writer_start)
            committed_txns += 1
            total_ops += 1

            for thread in reader_threads:
                thread.join(timeout=2)

    duration = time.perf_counter() - benchmark_start
    return {
        "workload": workload.name,
        "mode": mode.value,
        "throughput_tps": total_ops / duration if duration else 0.0,
        "avg_read_latency_ms": avg_ms(read_latencies),
        "p95_read_latency_ms": p95_ms(read_latencies),
        "avg_write_latency_ms": avg_ms(write_latencies),
        "blocked_read_count": blocked_reads,
        "wait_time_ms": wait_time_seconds * 1000.0,
        "committed_txn_count": committed_txns,
        "aborted_txn_count": aborted_txns,
        "version_count": engine.mvcc_manager.version_count("accounts"),
        "version_store_bytes": engine.mvcc_manager.storage_overhead_bytes(),
        "description": workload.description,
    }


def write_outputs(results: list[dict[str, object]]) -> None:
    benchmark_dir = ROOT / "benchmarks"
    csv_path = benchmark_dir / "benchmark_data.csv"
    fieldnames = [
        "workload",
        "mode",
        "throughput_tps",
        "avg_read_latency_ms",
        "p95_read_latency_ms",
        "avg_write_latency_ms",
        "blocked_read_count",
        "wait_time_ms",
        "committed_txn_count",
        "aborted_txn_count",
        "version_count",
        "version_store_bytes",
        "description",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for result in results:
            writer.writerow(result)

    summary_path = benchmark_dir / "benchmark_results.md"
    lines = [
        "# Concurrency Benchmark Results",
        "",
        "| Workload | Mode | Throughput (tx/s) | Avg Read (ms) | P95 Read (ms) | Avg Write (ms) | Blocked Reads | Wait Time (ms) | Committed | Aborted | Versions | Version Bytes |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for result in results:
        lines.append(
            "| {workload} | {mode} | {throughput_tps:.2f} | {avg_read_latency_ms:.3f} | {p95_read_latency_ms:.3f} | "
            "{avg_write_latency_ms:.3f} | {blocked_read_count} | {wait_time_ms:.3f} | {committed_txn_count} | "
            "{aborted_txn_count} | {version_count} | {version_store_bytes} |".format(**result)
        )

    lines.extend(
        [
            "",
            "Highlights:",
            "- `2PL` read latency rises sharply on the hot-key workloads because readers wait behind the writer's exclusive lock window.",
            "- `MVCC` keeps the writer hold time but lets readers return from their snapshot, which cuts blocked-read counts and cumulative wait time.",
            "- MVCC pays for that read concurrency with a larger version store and more versions to maintain.",
        ]
    )
    summary_path.write_text("\n".join(lines), encoding="utf-8")

    write_benchmark_report(results)
    print(f"Wrote {csv_path}")
    print(f"Wrote {summary_path}")
    print(f"Wrote {ROOT / 'docs' / 'BENCHMARK_REPORT.md'}")


def write_benchmark_report(results: list[dict[str, object]]) -> None:
    grouped: dict[str, dict[str, dict[str, object]]] = {}
    for result in results:
        grouped.setdefault(str(result["workload"]), {})[str(result["mode"])] = result

    baseline_rows: list[dict[str, str]] = []
    baseline_path = ROOT / "benchmarks" / "baseline_benchmark_data.csv"
    if baseline_path.exists():
        with baseline_path.open("r", newline="", encoding="utf-8") as handle:
            baseline_rows = list(csv.DictReader(handle))

    lines = [
        "# Track B Benchmark Report",
        "",
        "## Objective",
        "",
        "- Compare the required strict `2PL` baseline against the Track B `MVCC` extension on the same engine and schema.",
        "- Measure how much read blocking, latency, and throughput change when snapshot reads replace shared-lock reads under contention.",
        "- Keep the benchmark tied directly to the implemented system rather than synthetic external tooling.",
        "",
        "## Environment Note",
        "",
        "- The checked-in artifacts were generated by the repository benchmark scripts in a local single-process Python runtime.",
        "- Absolute throughput is host-dependent; the important signal is the relative difference between `2PL` and `MVCC` in the same execution environment.",
        "",
        "## Workloads",
        "",
        "| Workload | Readers/Writer | Writers/Round | Rounds | Hold Time (ms) | Description |",
        "| --- | ---: | ---: | ---: | ---: | --- |",
    ]

    for workload in WORKLOADS:
        lines.append(
            f"| {workload.name} | {workload.readers_per_writer} | {workload.writers_per_round} | "
            f"{workload.rounds} | {workload.hold_seconds * 1000.0:.0f} | {workload.description} |"
        )

    lines.extend(
        [
            "",
            "## Methodology",
            "",
            "- Every run creates the same `accounts` table and seeds identical starting rows.",
            "- A writer performs a logical update as `DELETE` plus `INSERT` on the same key inside one transaction.",
            "- Readers repeatedly issue `SELECT * FROM accounts WHERE id = hot_key;` while the writer intentionally holds the key for a short interval.",
            "- `2PL` and `MVCC` are executed with the same workload definitions so their behavior can be compared directly.",
            "",
            "## Metrics",
            "",
            "- throughput in transactions per second",
            "- average and P95 read latency",
            "- average write latency",
            "- blocked-read count and total wait time",
            "- committed and aborted transactions",
            "- version count and version-store bytes",
            "",
            "## Baseline Engine Microbenchmarks",
            "",
        ]
    )

    if baseline_rows:
        lines.extend(
            [
                "| Benchmark | Duration (s) | Rows/Events |",
                "| --- | ---: | ---: |",
            ]
        )
        for row in baseline_rows:
            lines.append(
                f"| {row['benchmark']} | {row['duration_seconds']} | {row['rows_or_events']} |"
            )
    else:
        lines.append(
            "- Baseline benchmark artifacts were not present when this report was generated. Run `python benchmarks/run_baseline.py` first to include them."
        )

    lines.extend(
        [
            "",
            "## Concurrency Results",
            "",
            "| Workload | Mode | Throughput (tx/s) | Avg Read (ms) | P95 Read (ms) | Avg Write (ms) | Blocked Reads | Wait Time (ms) | Committed | Aborted | Versions | Version Bytes |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )

    for result in results:
        lines.append(
            "| {workload} | {mode} | {throughput_tps:.2f} | {avg_read_latency_ms:.3f} | {p95_read_latency_ms:.3f} | "
            "{avg_write_latency_ms:.3f} | {blocked_read_count} | {wait_time_ms:.3f} | {committed_txn_count} | "
            "{aborted_txn_count} | {version_count} | {version_store_bytes} |".format(**result)
        )

    lines.extend(
        [
            "",
            "## Analysis",
            "",
            "The strongest signal appears on hot keys and read-heavy traffic, where the concurrency-control choice dominates total latency. In `2PL`, readers must wait behind a writer's exclusive lock window. In `MVCC`, those same readers can continue on their snapshot, so blocked reads collapse even though writers still serialize their own updates.",
            "",
        ]
    )

    for workload in WORKLOADS:
        two_pl = grouped[workload.name]["2PL"]
        mvcc = grouped[workload.name]["MVCC"]
        lines.extend(
            [
                f"### {workload.name}",
                "",
                f"- Setup: {workload.description}",
                f"- Throughput: `2PL` = `{two_pl['throughput_tps']:.2f} tx/s`, `MVCC` = `{mvcc['throughput_tps']:.2f} tx/s`.",
                f"- Blocked reads: `2PL` = `{two_pl['blocked_read_count']}`, `MVCC` = `{mvcc['blocked_read_count']}`.",
                f"- Read latency: `2PL` average/P95 = `{two_pl['avg_read_latency_ms']:.3f}` / `{two_pl['p95_read_latency_ms']:.3f}` ms, `MVCC` average/P95 = `{mvcc['avg_read_latency_ms']:.3f}` / `{mvcc['p95_read_latency_ms']:.3f}` ms.",
                f"- Write latency: `2PL` = `{two_pl['avg_write_latency_ms']:.3f}` ms, `MVCC` = `{mvcc['avg_write_latency_ms']:.3f}` ms.",
                f"- Version overhead: `{mvcc['version_count']}` versions and `{mvcc['version_store_bytes']}` bytes in the MVCC version store for this workload.",
                "",
            ]
        )

    lines.extend(
        [
            "## Why MVCC Reduced Read Blocking",
            "",
            "- In `2PL`, a reader requests a shared lock on the same logical key that the writer is already updating, so the reader waits for commit or rollback.",
            "- In `MVCC`, the reader does not take a shared lock for visibility and instead reads the newest committed version at or before its snapshot timestamp.",
            "",
            "## Where MVCC Added Overhead",
            "",
            "- Every logical change leaves another committed version in the version store, so metadata grows faster than in the lock-only baseline path.",
            "- Reads pay a visibility-check cost, and commits must publish staged versions with commit timestamps.",
            "",
            "## Threats To Validity",
            "",
            "- The benchmark is intentionally narrow and emphasizes hot-key reader/writer conflicts rather than broad OLTP realism.",
            "- The implementation is single-process Python, so absolute throughput numbers should not be generalized to other runtimes.",
            "- The engine retains old versions and does not vacuum them yet, so long-running benchmark sessions would magnify storage overhead.",
            "",
            "## Conclusion",
            "",
            "- The baseline `2PL` engine remains correct and testable, but it blocks readers under write contention.",
            "- The Track B `MVCC` mode preserves the same storage and indexing stack while replacing blocking read visibility with snapshot visibility.",
            "- On the checked-in reference workloads, MVCC consistently removes blocked reads and improves read-heavy throughput, at the cost of additional version-history storage and bookkeeping.",
        ]
    )

    report_path = ROOT / "docs" / "BENCHMARK_REPORT.md"
    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    runtime_root = ROOT / "benchmarks" / "runtime"
    shutil.rmtree(runtime_root, ignore_errors=True)
    runtime_root.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, object]] = []
    for workload in WORKLOADS:
        for mode in (TransactionMode.TWO_PL, TransactionMode.MVCC):
            results.append(run_workload(mode, workload))
    write_outputs(results)


if __name__ == "__main__":
    main()
