import os
import sys
import shutil
import time
import threading

# Add src/ to path so we can import minidb
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../src")))

from minidb import Database, WriteWriteConflict, DuplicateKeyError

def clean_db_dir(path):
    if os.path.exists(path):
        shutil.rmtree(path)

def run_basic_demo(db_dir):
    print("\n" + "="*50)
    print("DEMO 1: BASIC CRUD, INDEXING, AND SQL SELECT / JOIN")
    print("="*50)
    
    db = Database(db_dir)
    
    # 1. Create tables
    print(db.create_table("users", {"id": "INT", "name": "VARCHAR", "age": "INT"}, "id"))
    print(db.create_table("orders", {"order_id": "INT", "user_id": "INT", "product": "VARCHAR"}, "order_id"))
    
    # 2. Insert data
    print("\n--- Inserting data ---")
    db.execute_statement("INSERT INTO users VALUES (1, 'Alice', 25)")
    db.execute_statement("INSERT INTO users VALUES (2, 'Bob', 30)")
    db.execute_statement("INSERT INTO users VALUES (3, 'Charlie', 22)")
    
    db.execute_statement("INSERT INTO orders VALUES (101, 1, 'Laptop')")
    db.execute_statement("INSERT INTO orders VALUES (102, 2, 'Phone')")
    db.execute_statement("INSERT INTO orders VALUES (103, 1, 'Book')")
    
    # Test primary key uniqueness
    try:
        db.execute_statement("INSERT INTO users VALUES (1, 'DuplicateAlice', 25)")
        print("FAIL: Duplicate primary key inserted!")
    except DuplicateKeyError as e:
        print(f"PASS: Duplicate primary key blocked: {e}")
        
    # 3. Create index
    print("\n--- Creating secondary index on orders(user_id) ---")
    print(db.create_index("orders", "user_id"))

    # 4. Select queries
    print("\n--- Running SELECT queries ---")
    rows = db.execute_statement("SELECT id, name, age FROM users WHERE age >= 25")
    print("Users age >= 25:")
    for r in rows:
        print(" ", r)
        
    # 5. Join queries
    print("\n--- Running JOIN query ---")
    joined = db.execute_statement(
        "SELECT users.name, orders.product FROM users JOIN orders ON users.id = orders.user_id WHERE users.id = 1"
    )
    print("Orders joined with users for user_id = 1:")
    for r in joined:
        print(" ", r)

    # 6. Delete query
    print("\n--- Running DELETE query ---")
    print(db.execute_statement("DELETE FROM users WHERE id = 3"))
    remaining = db.execute_statement("SELECT id, name FROM users")
    print("Remaining users:")
    for r in remaining:
        print(" ", r)

    db.flush_all_dirty()
    db.wal.close()


def run_mvcc_demo(db_dir):
    print("\n" + "="*50)
    print("DEMO 2: MVCC CONCURRENCY & SNAPSHOT ISOLATION")
    print("="*50)
    
    db = Database(db_dir)
    db.create_table("accounts", {"id": "INT", "name": "VARCHAR", "balance": "INT"}, "id")
    db.execute_statement("INSERT INTO accounts VALUES (1, 'Alice', 1000)")
    db.execute_statement("INSERT INTO accounts VALUES (2, 'Bob', 500)")
    
    print("Initial Accounts:")
    print(" ", db.execute_statement("SELECT * FROM accounts"))

    # Tx 1 begins
    print("\n[Tx 1] Begins...")
    tx1 = db.tm.begin_tx()
    db.wal.log_begin(tx1.tx_id)
    
    # Tx 2 begins
    print("[Tx 2] Begins...")
    tx2 = db.tm.begin_tx()
    db.wal.log_begin(tx2.tx_id)
    
    # Tx 1 updates Alice's balance (by deleting and reinserting / simulating update)
    print("[Tx 1] Deletes Alice (id=1)")
    db.execute_parsed({"type": "DELETE", "table": "accounts", "where_col": "id", "where_val": 1}, tx1)
    print("[Tx 1] Inserts Alice (id=1) with balance 1200")
    db.execute_parsed({"type": "INSERT", "table": "accounts", "values": [1, 'Alice', 1200]}, tx1)
    
    # Tx 2 reads Alice's balance. Should see 1000 (old balance)
    print("\n[Tx 2] Reads Alice's balance (should be 1000 due to isolation):")
    res_tx2 = db.execute_parsed({
        "type": "SELECT", "columns": ["*"], "table": "accounts",
        "join_table": None, "where_col": "id", "where_op": "=", "where_val": 1
    }, tx2)
    print("  Tx 2 sees:", res_tx2[0] if res_tx2 else "None")
    assert res_tx2[0]["balance"] == 1000, "MVCC Isolation failed!"
    
    # Tx 1 commits
    print("\n[Tx 1] Commits...")
    db.tm.commit_tx(tx1.tx_id)
    db.wal.log_commit(tx1.tx_id)
    
    # Tx 2 reads Alice's balance again. Should STILL see 1000 because Tx 2's snapshot was taken before Tx 1 committed
    print("[Tx 2] Reads Alice's balance again (should STILL be 1000):")
    res_tx2_again = db.execute_parsed({
        "type": "SELECT", "columns": ["*"], "table": "accounts",
        "join_table": None, "where_col": "id", "where_op": "=", "where_val": 1
    }, tx2)
    print("  Tx 2 sees:", res_tx2_again[0] if res_tx2_again else "None")
    assert res_tx2_again[0]["balance"] == 1000, "MVCC Snapshot Isolation failed!"
    
    # Tx 3 begins now. Should see 1200 because Tx 1 committed before Tx 3 began
    print("\n[Tx 3] Begins...")
    tx3 = db.tm.begin_tx()
    db.wal.log_begin(tx3.tx_id)
    res_tx3 = db.execute_parsed({
        "type": "SELECT", "columns": ["*"], "table": "accounts",
        "join_table": None, "where_col": "id", "where_op": "=", "where_val": 1
    }, tx3)
    print("  Tx 3 sees:", res_tx3[0] if res_tx3 else "None")
    assert res_tx3[0]["balance"] == 1200, "MVCC visibility failed!"
    
    # Clean up Tx 2 and Tx 3
    db.tm.commit_tx(tx2.tx_id)
    db.wal.log_commit(tx2.tx_id)
    db.tm.commit_tx(tx3.tx_id)
    db.wal.log_commit(tx3.tx_id)

    # 7. Write-Write Conflict Detection
    print("\n--- Testing Write-Write Conflict detection ---")
    tx4 = db.tm.begin_tx()
    tx5 = db.tm.begin_tx()
    
    print("[Tx 4] Deletes Bob (id=2)...")
    db.execute_parsed({"type": "DELETE", "table": "accounts", "where_col": "id", "where_val": 2}, tx4)
    
    print("[Tx 5] Attempts to delete Bob (id=2) concurrently...")
    try:
        db.execute_parsed({"type": "DELETE", "table": "accounts", "where_col": "id", "where_val": 2}, tx5)
        print("FAIL: Concurrency write-write conflict not detected!")
    except WriteWriteConflict as e:
        print(f"PASS: Write-write conflict successfully prevented: {e}")
        # Roll back Tx 5
        db.tm.abort_tx(tx5.tx_id)
        db.wal.log_abort(tx5.tx_id)
        db.rollback_tx_memory(tx5.tx_id)

    print("[Tx 4] Commits deletion...")
    db.tm.commit_tx(tx4.tx_id)
    db.wal.log_commit(tx4.tx_id)

    db.flush_all_dirty()
    db.wal.close()


def run_crash_recovery_demo(db_dir):
    print("\n" + "="*50)
    print("DEMO 3: WAL CRASH RECOVERY (REDO & UNDO)")
    print("="*50)
    
    # 1. Start clean db and insert some committed rows
    db = Database(db_dir)
    db.create_table("inventory", {"id": "INT", "item": "VARCHAR", "qty": "INT"}, "id")
    db.execute_statement("INSERT INTO inventory VALUES (1, 'Apples', 50)")
    db.execute_statement("INSERT INTO inventory VALUES (2, 'Oranges', 30)")
    
    print("Initially committed data:")
    print(" ", db.execute_statement("SELECT * FROM inventory"))
    
    # 2. Start some active transactions to simulate crash with uncommitted writes
    tx_uncommitted = db.tm.begin_tx()
    db.wal.log_begin(tx_uncommitted.tx_id)
    
    print(f"\n[Tx {tx_uncommitted.tx_id}] (Uncommitted) Inserts Grapes (id=3)...")
    db.execute_parsed({"type": "INSERT", "table": "inventory", "values": [3, 'Grapes', 100]}, tx_uncommitted)
    
    print(f"[Tx {tx_uncommitted.tx_id}] (Uncommitted) Deletes Oranges (id=2)...")
    db.execute_parsed({"type": "DELETE", "table": "inventory", "where_col": "id", "where_val": 2}, tx_uncommitted)
    
    # Force pages to disk to simulate dirty uncommitted pages on disk
    db.flush_all_dirty()
    print("Dirty uncommitted changes flushed to disk database file.")
    
    # 3. Simulate sudden system crash (close file descriptors without writing COMMIT/ABORT log, close DB)
    print("\n*** SIMULATING HARD CRASH ***")
    db.wal.close()
    
    # 4. Recover Database on Startup
    print("\nRestarting Database and running recovery...")
    recovered_db = Database(db_dir)
    
    # 5. Verify results
    print("\nVerifying recovered inventory table data:")
    rows = recovered_db.execute_statement("SELECT * FROM inventory")
    for r in rows:
        print(" ", r)
        
    # Checks:
    # Apples (id=1) must exist
    # Oranges (id=2) must exist (since Tx uncommitted deleted it, recovery should have UNDONE it)
    # Grapes (id=3) must NOT exist (since Tx uncommitted inserted it, recovery should have UNDONE it)
    items = {r["item"] for r in rows}
    assert "Apples" in items, "Committed Apples lost!"
    assert "Oranges" in items, "Uncommitted delete of Oranges not rolled back!"
    assert "Grapes" not in items, "Uncommitted insert of Grapes not rolled back!"
    
    print("PASS: WAL recovery successfully completed REDO and UNDO phases!")
    recovered_db.wal.close()


def run_performance_benchmarks(db_dir):
    print("\n" + "="*50)
    print("DEMO 4: CBO COST-BASED OPTIMIZATION & PERFORMANCE BENCHMARK")
    print("="*50)
    
    db = Database(db_dir)
    db.create_table("large_table", {"id": "INT", "value": "VARCHAR"}, "id")
    
    # Insert 500 rows
    print("Inserting 500 records...")
    tx = db.tm.begin_tx()
    db.wal.log_begin(tx.tx_id)
    for i in range(1, 501):
        db.execute_parsed({"type": "INSERT", "table": "large_table", "values": [i, f"Val{i}"]}, tx)
    db.tm.commit_tx(tx.tx_id)
    db.wal.log_commit(tx.tx_id)
    db.flush_all_dirty()
    
    print(f"Table size is now {db.get_heap_file('large_table').num_pages()} pages.")
    
    # Run optimizer scans
    print("\n--- Optimizer decision test for equality query ---")
    # Table scan vs Index scan comparison
    print("Executing equality search: SELECT * FROM large_table WHERE id = 250")
    
    # We measure performance of TableScan vs IndexScan
    # 1. Force table scan by temporarily removing index from catalog
    db.catalog["tables"]["large_table"]["indexes"] = []
    
    t0 = time.perf_counter()
    res_ts = db.execute_statement("SELECT * FROM large_table WHERE id = 250")
    t_ts = (time.perf_counter() - t0) * 1000.0
    print(f"  Table Scan query time: {t_ts:.3f} ms (Result: {res_ts})")
    
    # Restore index
    db.catalog["tables"]["large_table"]["indexes"] = ["id"]
    db.rebuild_indexes()
    
    t0 = time.perf_counter()
    res_is = db.execute_statement("SELECT * FROM large_table WHERE id = 250")
    t_is = (time.perf_counter() - t0) * 1000.0
    print(f"  Index Scan query time: {t_is:.3f} ms (Result: {res_is})")
    
    speedup = t_ts / max(0.001, t_is)
    print(f"  Index scan speedup factor: {speedup:.1f}x")
    
    db.wal.close()


if __name__ == "__main__":
    db_directory = "./test_minidb_data"
    
    clean_db_dir(db_directory)
    try:
        run_basic_demo(db_directory)
        clean_db_dir(db_directory)
        
        run_mvcc_demo(db_directory)
        clean_db_dir(db_directory)
        
        run_crash_recovery_demo(db_directory)
        clean_db_dir(db_directory)
        
        run_performance_benchmarks(db_directory)
        print("\nAll demos completed successfully!")
    finally:
        clean_db_dir(db_directory)
