import os
import shutil
import time
import math
import random
import threading
from typing import List
from src.minidb import MiniDB
from src.transactions.lock_manager import TransactionAbortException

def calculate_percentiles(times: List[float]) -> tuple:
    if not times:
        return 0.0, 0.0
    sorted_times = sorted(times)
    n = len(sorted_times)
    p50_idx = int(n * 0.50)
    p95_idx = int(n * 0.95)
    return sorted_times[p50_idx] * 1000.0, sorted_times[p95_idx] * 1000.0  # convert to ms

def run_benchmarks():
    data_dir = "benchmarks/db_data"
    
    # Clean directory
    if os.path.exists(data_dir):
        shutil.rmtree(data_dir)
    os.makedirs(data_dir, exist_ok=True)

    print("--- STARTING BENCHMARKS ---")
    
    # 1. Insert Throughput
    print("Running Insert Throughput Benchmark...")
    db = MiniDB(data_dir=data_dir, verbose=False, txn_mode="2PL")
    db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    
    start_time = time.time()
    num_inserts = 10000
    db.execute_sql("BEGIN;")
    for i in range(1, num_inserts + 1):
        db.execute_sql(f"INSERT INTO students VALUES ({i}, 'Student_{i}', {20 + (i % 10)});")
    db.execute_sql("COMMIT;")
    duration = time.time() - start_time
    insert_throughput = num_inserts / duration
    print(f"Insert Throughput: {insert_throughput:.2f} rows/sec ({num_inserts} rows in {duration:.2f}s)")

    # 2. SeqScan vs IndexScan Latency
    print("Running SeqScan vs IndexScan Latency Benchmark...")
    # Index Scan (using primary key id)
    index_times = []
    for _ in range(100):
        search_id = random.randint(1, num_inserts)
        t0 = time.time()
        # Query will trigger IndexScan cost evaluation
        db.execute_sql(f"SELECT * FROM students WHERE id = {search_id};")
        index_times.append(time.time() - t0)

    # Force SeqScan (by doing SELECT without index, or disabling optimizer temporarily)
    # We can query on age without index or do a condition on id with a function, or we can just
    # temporarily remove the index from the db descriptor to force SeqScan!
    original_indexes = db.indexes["students"]
    db.indexes["students"] = {}  # Remove indexes temporarily
    
    seq_times = []
    for _ in range(100):
        search_id = random.randint(1, num_inserts)
        t0 = time.time()
        db.execute_sql(f"SELECT * FROM students WHERE id = {search_id};")
        seq_times.append(time.time() - t0)
        
    db.indexes["students"] = original_indexes  # Restore index
    
    seq_p50, seq_p95 = calculate_percentiles(seq_times)
    idx_p50, idx_p95 = calculate_percentiles(index_times)
    print(f"SeqScan: p50={seq_p50:.2f}ms, p95={seq_p95:.2f}ms")
    print(f"IndexScan: p50={idx_p50:.2f}ms, p95={idx_p95:.2f}ms")

    # 3. Concurrent Transactions
    print("Running Concurrent Transactions Benchmark...")
    db_concur = MiniDB(data_dir=os.path.join(data_dir, "concur"), verbose=False, txn_mode="2PL")
    db_concur.execute_sql("CREATE TABLE accounts (id INT PRIMARY KEY, balance INT);")
    for i in range(1, 11):
        db_concur.execute_sql(f"INSERT INTO accounts VALUES ({i}, 1000);")

    lock_aborts = 0
    total_txns = 0
    def txn_worker(thread_id, num_txns):
        nonlocal lock_aborts, total_txns
        for _ in range(num_txns):
            # Pick two random accounts
            a1 = random.randint(1, 10)
            a2 = random.randint(1, 10)
            if a1 == a2:
                continue
                
            # To simulate deadlock, we don't enforce locking order
            try:
                db_concur.execute_sql("BEGIN;")
                # Read both balances
                db_concur.execute_sql(f"SELECT * FROM accounts WHERE id = {a1};")
                db_concur.execute_sql(f"SELECT * FROM accounts WHERE id = {a2};")
                # Modify both
                db_concur.execute_sql(f"DELETE FROM accounts WHERE id = {a1};")
                db_concur.execute_sql(f"INSERT INTO accounts VALUES ({a1}, 950);")
                db_concur.execute_sql("COMMIT;")
            except TransactionAbortException:
                lock_aborts += 1
            except Exception:
                pass
            finally:
                total_txns += 1

    threads = []
    num_threads = 5
    txns_per_thread = 100
    t0 = time.time()
    for tid in range(num_threads):
        t = threading.Thread(target=txn_worker, args=(tid, txns_per_thread))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()
    concur_duration = time.time() - t0
    concur_throughput = total_txns / concur_duration
    deadlock_rate = (lock_aborts / total_txns) * 100 if total_txns > 0 else 0
    print(f"Concurrency Throughput: {concur_throughput:.2f} txns/sec ({total_txns} txns in {concur_duration:.2f}s)")
    print(f"Deadlock rate: {deadlock_rate:.2f}% ({lock_aborts} aborts)")

    # 4. MVCC vs 2PL Read/Write Concurrency
    print("Running MVCC vs 2PL Read/Write Concurrency Benchmark...")
    # Under 2PL: Readers acquire SHARED locks, Writers acquire EXCLUSIVE locks.
    # Therefore, writers block readers and vice versa.
    # Under MVCC: Readers never block writers, and writers never block readers.
    
    def run_rw_benchmark(mode: str) -> float:
        db_rw = MiniDB(data_dir=os.path.join(data_dir, f"rw_{mode}"), verbose=False, txn_mode=mode)
        db_rw.execute_sql("CREATE TABLE test_rw (id INT PRIMARY KEY, val INT);")
        db_rw.execute_sql("INSERT INTO test_rw VALUES (1, 100);")
        
        # We start a reader thread and writer thread executing concurrently
        reader_durations = []
        writer_durations = []
        
        stop_event = threading.Event()
        
        def reader():
            while not stop_event.is_set():
                t_start = time.time()
                try:
                    db_rw.execute_sql("BEGIN;")
                    db_rw.execute_sql("SELECT * FROM test_rw WHERE id = 1;")
                    db_rw.execute_sql("COMMIT;")
                    reader_durations.append(time.time() - t_start)
                except Exception:
                    pass
                time.sleep(0.01)
                
        def writer():
            while not stop_event.is_set():
                t_start = time.time()
                try:
                    db_rw.execute_sql("BEGIN;")
                    db_rw.execute_sql("DELETE FROM test_rw WHERE id = 1;")
                    db_rw.execute_sql(f"INSERT INTO test_rw VALUES (1, {random.randint(100, 1000)});")
                    db_rw.execute_sql("COMMIT;")
                    writer_durations.append(time.time() - t_start)
                except Exception:
                    pass
                time.sleep(0.01)

        tr = threading.Thread(target=reader)
        tw = threading.Thread(target=writer)
        tr.start()
        tw.start()
        
        time.sleep(2.0)  # Run for 2 seconds
        stop_event.set()
        tr.join()
        tw.join()
        
        avg_reader = sum(reader_durations) / len(reader_durations) if reader_durations else 0
        return avg_reader * 1000.0  # ms

    avg_read_time_2pl = run_rw_benchmark("2PL")
    avg_read_time_mvcc = run_rw_benchmark("MVCC")
    print(f"2PL Average Reader Response Time: {avg_read_time_2pl:.2f}ms")
    print(f"MVCC Average Reader Response Time: {avg_read_time_mvcc:.2f}ms")

    # 5. Recovery Time
    print("Running Recovery Time Benchmark...")
    db_rec_test = MiniDB(data_dir=os.path.join(data_dir, "recovery"), verbose=False, txn_mode="2PL")
    db_rec_test.execute_sql("CREATE TABLE dummy (id INT PRIMARY KEY, num INT);")
    db_rec_test.execute_sql("BEGIN;")
    for i in range(1000):
        db_rec_test.execute_sql(f"INSERT INTO dummy VALUES ({i}, {i * 2});")
    db_rec_test.execute_sql("COMMIT;")
    
    # Close WAL without checkpointing (buffer pool has uncommitted changes in memory, but WAL is fully flushed)
    db_rec_test.wal_manager.close()
    
    # Delete DB & IDX files to simulate disk crash before pages are flushed
    db_files_path = os.path.join(data_dir, "recovery", "dummy.db")
    idx_files_path = os.path.join(data_dir, "recovery", "dummy_id.idx")
    if os.path.exists(db_files_path):
        os.remove(db_files_path)
    if os.path.exists(idx_files_path):
        os.remove(idx_files_path)
        
    # Re-instantiate DB and measure recovery duration
    t_start = time.time()
    db_recovered = MiniDB(data_dir=os.path.join(data_dir, "recovery"), verbose=False, txn_mode="2PL")
    recovery_duration = (time.time() - t_start) * 1000.0  # ms
    print(f"Recovery Time: {recovery_duration:.2f}ms")

    # Write results to results.md
    results_content = f"""# MiniDB Relational Engine Benchmark Results

Benchmarks ran on a simulation environment.

| Benchmark Metric | Value | Details / Comparison |
|------------------|-------|----------------------|
| **Insert Throughput** | {insert_throughput:.2f} rows/sec | Inserting {num_inserts} rows to heap & primary index |
| **SeqScan p50 Latency** | {seq_p50:.2f} ms | Range scan/exact search without index |
| **SeqScan p95 Latency** | {seq_p95:.2f} ms | Range scan/exact search without index |
| **IndexScan p50 Latency** | {idx_p50:.2f} ms | Exact search on B+ Tree primary key index |
| **IndexScan p95 Latency** | {idx_p95:.2f} ms | Exact search on B+ Tree primary key index |
| **Concurrency Throughput** | {concur_throughput:.2f} txns/sec | 5 threads executing 100 txns each |
| **Concurrency Deadlock Rate** | {deadlock_rate:.2f} % | Percentage of transactions aborted due to deadlocks |
| **2PL Read Response Time** | {avg_read_time_2pl:.2f} ms | Reader latency with concurrent writer (blocked by locks) |
| **MVCC Read Response Time** | {avg_read_time_mvcc:.2f} ms | Reader latency with concurrent writer (never blocks) |
| **Crash Recovery Time** | {recovery_duration:.2f} ms | Analysis, Redo, and Undo passes for 1000 inserts |

"""
    results_path = "benchmarks/results.md"
    os.makedirs(os.path.dirname(results_path), exist_ok=True)
    with open(results_path, "w") as f:
        f.write(results_content)
    print(f"Benchmark results written to {results_path}")

if __name__ == "__main__":
    run_benchmarks()
