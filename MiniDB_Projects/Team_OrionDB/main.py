import os
import sys
# Add current directory to sys.path to enable local imports
sys.path.append(os.path.abspath(os.path.dirname(__file__)))

import time
import shutil
import threading
from src.database import MiniDB

def print_banner(title):
    print("\n" + "="*70)
    print(f" {title.center(68)} ")
    print("="*70)

def print_result_table(rows):
    if not rows:
        print("Empty set (0 rows)")
        return
        
    keys = list(rows[0].keys())
    # Determine column widths
    widths = {k: len(k) for k in keys}
    for row in rows:
        for k in keys:
            widths[k] = max(widths[k], len(str(row[k])))
            
    header = " | ".join(k.ljust(widths[k]) for k in keys)
    divider = "-+-".join("-" * widths[k] for k in keys)
    print(header)
    print(divider)
    for row in rows:
        print(" | ".join(str(row[k]).ljust(widths[k]) for k in keys))
    print(f"({len(rows)} rows in set)")

def run_sql_cli(db):
    print_banner("MiniDB Interactive SQL CLI")
    print("Type your SQL commands. Type 'exit' or 'back' to return to the main menu.")
    print("Supports: CREATE TABLE, SELECT, INSERT, DELETE (case-insensitive).")
    
    while True:
        try:
            sql = input("\nminidb> ").strip()
            if not sql:
                continue
            if sql.lower() in ("exit", "quit", "back"):
                break
                
            start_time = time.time()
            res = db.execute_sql(sql)
            duration = time.time() - start_time
            
            if isinstance(res, list):
                print_result_table(res)
            else:
                print(res)
            print(f"Execution time: {duration:.4f}s")
        except Exception as e:
            print(f"Error: {e}")

def demo_optimizer():
    print_banner("Cost-Based Optimizer Demo")
    db_dir = "./demo_optimizer_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
        
    db = MiniDB(db_dir, is_mvcc=False)
    
    # 1. Access Path Optimization Demo
    print("Step 1: Creating table 'users' and populating 50 records...")
    db.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50), age INT)")
    for i in range(1, 51):
        db.execute_sql(f"INSERT INTO users VALUES ({i}, 'user{i}', {20 + (i % 30)})")
    db.checkpoint()
    db._update_optimizer_statistics()

    print("\n--- Running PK point query: SELECT name FROM users WHERE id = 25 ---")
    print("Expected: Optimizer chooses IndexScan because condition is on PK and cost is lower.")
    res = db.execute_sql("SELECT name FROM users WHERE id = 25")
    print("Result:")
    print_result_table(res)

    print("\n--- Running non-PK query: SELECT name FROM users WHERE age = 30 ---")
    print("Expected: Optimizer chooses SeqScan because 'age' has no index.")
    res = db.execute_sql("SELECT name FROM users WHERE age = 30")
    print("Result:")
    print_result_table(res)

    # 2. Join Order Optimization Demo
    print("\nStep 2: Creating table 'orders' and populating 5 records...")
    db.execute_sql("CREATE TABLE orders (order_id INT PRIMARY KEY, user_id INT, amount INT)")
    for i in range(1, 6):
        db.execute_sql(f"INSERT INTO orders VALUES ({i}, {i * 10}, {i * 100})")
    db.checkpoint()
    db._update_optimizer_statistics()

    print("\n--- Running Join Query: SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id ---")
    print("Expected: Optimizer selects orders JOIN users because orders (5 rows) is smaller than users (50 rows),")
    print("and the join condition 'users.id' is the primary key of users (indexed). This enables Index Nested Loop Join.")
    res = db.execute_sql("SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id")
    print("Result:")
    print_result_table(res)

    db.close()
    shutil.rmtree(db_dir)

def demo_deadlocks_2pl():
    print_banner("Strict 2PL & Deadlock Detection Demo")
    db_dir = "./demo_deadlock_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
        
    db = MiniDB(db_dir, is_mvcc=False)
    db.execute_sql("CREATE TABLE accounts (id INT PRIMARY KEY, balance INT)")
    db.execute_sql("INSERT INTO accounts VALUES (1, 100)")
    db.execute_sql("INSERT INTO accounts VALUES (2, 200)")
    db.checkpoint()

    print("Database State:")
    print_result_table(db.execute_sql("SELECT * FROM accounts"))

    t1_status = []
    t2_status = []

    def txn1_worker():
        tx = db.tx_manager.begin()
        print(f"\n[Txn 1 (ID {tx.tx_id})] Started. Acquiring S lock on ID=1...")
        try:
            db.execute_sql(f"SELECT balance FROM accounts WHERE id = 1", tx)
            print(f"[Txn 1] S lock on ID=1 acquired. Sleeping 1.5s...")
            time.sleep(1.5)
            
            print(f"[Txn 1] Attempting to select/delete ID=2 (needs lock)...")
            db.execute_sql(f"DELETE FROM accounts WHERE id = 2", tx)
            print(f"[Txn 1] Exclusive lock on ID=2 acquired! Committing...")
            db.tx_manager.commit(tx.tx_id)
            t1_status.append("COMMITTED")
        except Exception as e:
            print(f"[Txn 1] Transaction aborted: {e}")
            db.tx_manager.abort(tx.tx_id)
            t1_status.append("ABORTED")

    def txn2_worker():
        time.sleep(0.5) # Wait for Txn 1 to lock ID=1
        tx = db.tx_manager.begin()
        print(f"\n[Txn 2 (ID {tx.tx_id})] Started. Acquiring S lock on ID=2...")
        try:
            db.execute_sql(f"SELECT balance FROM accounts WHERE id = 2", tx)
            print(f"[Txn 2] S lock on ID=2 acquired. Sleeping 1.5s...")
            time.sleep(1.5)
            
            print(f"[Txn 2] Attempting to select/delete ID=1 (needs lock)...")
            db.execute_sql(f"DELETE FROM accounts WHERE id = 1", tx)
            print(f"[Txn 2] Exclusive lock on ID=1 acquired! Committing...")
            db.tx_manager.commit(tx.tx_id)
            t2_status.append("COMMITTED")
        except Exception as e:
            print(f"[Txn 2] Transaction aborted: {e}")
            db.tx_manager.abort(tx.tx_id)
            t2_status.append("ABORTED")

    print("\nStarting concurrent transaction threads to induce deadlock...")
    thread1 = threading.Thread(target=txn1_worker)
    thread2 = threading.Thread(target=txn2_worker)

    thread1.start()
    thread2.start()

    thread1.join()
    thread2.join()

    print("\nDeadlock demo finished.")
    print(f"Transaction 1 Status: {t1_status[0] if t1_status else 'UNKNOWN'}")
    print(f"Transaction 2 Status: {t2_status[0] if t2_status else 'UNKNOWN'}")
    print("Note: The younger transaction was aborted by the LockManager cycle detector, breaking the cycle and allowing the other to complete!")

    db.close()
    shutil.rmtree(db_dir)

def demo_mvcc_isolation():
    print_banner("MVCC Snapshot Isolation Demo")
    db_dir = "./demo_mvcc_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
        
    db = MiniDB(db_dir, is_mvcc=True)
    db.execute_sql("CREATE TABLE accounts (id INT PRIMARY KEY, val INT)")
    db.execute_sql("INSERT INTO accounts VALUES (1, 100)")
    db.checkpoint()

    print("Initial account status:")
    print_result_table(db.execute_sql("SELECT * FROM accounts"))

    # Begin Tx 1
    tx1 = db.tx_manager.begin()
    print(f"\n[Tx 1 (ID {tx1.tx_id})] Began. Reading balance of ID=1...")
    r1 = db.execute_sql("SELECT val FROM accounts WHERE id = 1", tx1)
    print(f"[Tx 1] Read balance: {r1[0]['val']}")

    # Begin Tx 2 and modify
    tx2 = db.tx_manager.begin()
    print(f"\n[Tx 2 (ID {tx2.tx_id})] Began. Deleting ID=1 and inserting new value (balance=200)...")
    db.execute_sql("DELETE FROM accounts WHERE id = 1", tx2)
    db.execute_sql("INSERT INTO accounts VALUES (1, 200)", tx2)
    print(f"[Tx 2] Updates completed. Committing Tx 2...")
    db.tx_manager.commit(tx2.tx_id)
    print(f"[Tx 2] Committed.")

    # Tx 1 reads again
    print(f"\n[Tx 1 (ID {tx1.tx_id})] Reading balance of ID=1 again (under snapshot isolation)...")
    r2 = db.execute_sql("SELECT val FROM accounts WHERE id = 1", tx1)
    print(f"[Tx 1] Read balance: {r2[0]['val']}")
    print("Note: Balance is still 100 because Tx 1 reads from its snapshot taken before Tx 2 committed!")

    # Tx 1 commits
    db.tx_manager.commit(tx1.tx_id)
    print(f"[Tx 1] Committed.")

    # New transaction reads
    tx3 = db.tx_manager.begin()
    print(f"\n[Tx 3 (ID {tx3.tx_id})] Began. Reading balance of ID=1...")
    r3 = db.execute_sql("SELECT val FROM accounts WHERE id = 1", tx3)
    print(f"[Tx 3] Read balance: {r3[0]['val']}")
    print("Note: Balance is now 200 because Tx 3's snapshot was created after Tx 2 committed!")
    db.tx_manager.commit(tx3.tx_id)

    db.close()
    shutil.rmtree(db_dir)

def demo_wal_recovery():
    print_banner("WAL & Crash Recovery Demo")
    db_dir = "./demo_recovery_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
        
    db = MiniDB(db_dir, is_mvcc=False)
    db.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50))")
    
    # 1. Txn 1: Committed Write
    tx1 = db.tx_manager.begin()
    print(f"[Tx 1 (ID {tx1.tx_id})] Inserting committed user 'Alice'...")
    db.execute_sql(f"INSERT INTO users VALUES (1, 'Alice')", tx1)
    db.tx_manager.commit(tx1.tx_id)
    print("[Tx 1] Committed.")

    # 2. Txn 2: Uncommitted Write (will be lost in crash)
    tx2 = db.tx_manager.begin()
    print(f"[Tx 2 (ID {tx2.tx_id})] Inserting uncommitted user 'Bob'...")
    db.execute_sql(f"INSERT INTO users VALUES (2, 'Bob')", tx2)
    print("[Tx 2] Inserted but NOT committed.")

    # Simulate crash:
    # Close files without flushing pages.
    # To do this, we shut down the disk manager immediately, discarding the buffer pool in memory.
    # The WAL log file will contain the updates, but they are not committed for Txn 2.
    print("\nSimulating crash: closing database files immediately, discarding dirty buffer pool frames...")
    db.disk_manager.close()
    db.wal_manager.close()

    print("\nDatabase crashed. Restarting and initiating recovery...")
    # Re-open database
    db_recovered = MiniDB(db_dir, is_mvcc=False)
    
    # Run recovery
    db_recovered.recover()

    print("\nQuerying table 'users' after recovery:")
    res = db_recovered.execute_sql("SELECT * FROM users")
    print_result_table(res)
    print("Expected: 'Alice' (ID 1) is preserved. 'Bob' (ID 2) is rolled back because Txn 2 did not commit!")

    db_recovered.close()
    shutil.rmtree(db_dir)

def main():
    print("="*70)
    print("                     Welcome to MiniDB DBMS")
    print("="*70)
    
    db_dir = "./minidb_data"
    # Instantiate default database
    db = MiniDB(db_dir, is_mvcc=True)
    
    while True:
        print("\n" + "="*50)
        print("                  MAIN MENU")
        print("="*50)
        print("1. SQL Console CLI (Interactive)")
        print("2. Cost-Based Optimizer Demo")
        print("3. Strict 2PL & Deadlock Detection Demo")
        print("4. MVCC Snapshot Isolation Demo")
        print("5. WAL & Crash Recovery Demo")
        print("6. Run Concurrency Performance Benchmarks")
        print("7. Exit")
        
        choice = input("\nEnter choice (1-7): ").strip()
        if choice == '1':
            run_sql_cli(db)
        elif choice == '2':
            demo_optimizer()
        elif choice == '3':
            demo_deadlocks_2pl()
        elif choice == '4':
            demo_mvcc_isolation()
        elif choice == '5':
            demo_wal_recovery()
        elif choice == '6':
            # Close active db to run benchmarks in isolation
            db.close()
            from benchmarks.run_benchmarks import run_all_benchmarks
            run_all_benchmarks()
            # Reopen
            db = MiniDB(db_dir, is_mvcc=True)
        elif choice == '7':
            db.close()
            print("Goodbye!")
            break
        else:
            print("Invalid choice. Please select 1-7.")

if __name__ == "__main__":
    main()
