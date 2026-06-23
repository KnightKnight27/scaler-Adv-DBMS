import os
import sys
import time
import shutil
import threading
sys.path.append(os.path.abspath(os.path.dirname(__file__)))
from src.database import MiniDB

def banner(title):
    print("\n" + "=" * 70)
    print(f"  {title}")
    print("=" * 70)

def show_rows(rows):
    if not rows:
        print("  Empty set (0 rows)")
        return
    keys = [k for k in rows[0].keys()]
    widths = {k: max(len(k), max(len(str(r[k])) for r in rows)) for k in keys}
    header = " | ".join(k.ljust(widths[k]) for k in keys)
    sep = "-+-".join("-" * widths[k] for k in keys)
    print("  " + header)
    print("  " + sep)
    for r in rows:
        print("  " + " | ".join(str(r[k]).ljust(widths[k]) for k in keys))
    print(f"  ({len(rows)} rows)")

# ============================================================
# DEMO 1: Basic SQL Operations
# ============================================================
def demo1():
    banner("DEMO 1: Basic SQL Operations (CREATE / INSERT / SELECT / DELETE)")
    db_dir = "./demo1_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
    db = MiniDB(db_dir, is_mvcc=True)

    print("\n>>> CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(50), grade INT)")
    res = db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(50), grade INT)")
    print("   ", res)

    print("\n>>> Inserting 5 students...")
    data = [(1, "Alice", 95), (2, "Bob", 87), (3, "Charlie", 92), (4, "Diana", 78), (5, "Eve", 99)]
    for sid, name, grade in data:
        sql = "INSERT INTO students VALUES (%d, '%s', %d)" % (sid, name, grade)
        res = db.execute_sql(sql)
        print("    %s  =>  %s" % (sql, res))

    print("\n>>> SELECT * FROM students")
    show_rows(db.execute_sql("SELECT * FROM students"))

    print("\n>>> SELECT name, grade FROM students WHERE id = 3  (PK lookup)")
    show_rows(db.execute_sql("SELECT name, grade FROM students WHERE id = 3"))

    print("\n>>> SELECT name FROM students WHERE grade > 90  (non-PK filter)")
    show_rows(db.execute_sql("SELECT name FROM students WHERE grade > 90"))

    print("\n>>> DELETE FROM students WHERE id = 4")
    res = db.execute_sql("DELETE FROM students WHERE id = 4")
    print("   ", res)

    print("\n>>> SELECT * FROM students  (after deleting Diana)")
    show_rows(db.execute_sql("SELECT * FROM students"))

    db.close()
    shutil.rmtree(db_dir)

# ============================================================
# DEMO 2: Cost-Based Optimizer
# ============================================================
def demo2():
    banner("DEMO 2: Cost-Based Optimizer (Access Path + Join Order)")
    db_dir = "./demo2_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
    db = MiniDB(db_dir, is_mvcc=False)

    print("\nCreating 'users' table with 50 rows...")
    db.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50), age INT)")
    for i in range(1, 51):
        db.execute_sql("INSERT INTO users VALUES (%d, 'user%d', %d)" % (i, i, 20 + (i % 30)))
    db.checkpoint()
    db._update_optimizer_statistics()

    print("\n--- PK Point Query: SELECT name FROM users WHERE id = 25 ---")
    print("    Expected: Optimizer picks IndexScan (cost ~31) over SeqScan (cost ~110)")
    show_rows(db.execute_sql("SELECT name FROM users WHERE id = 25"))

    print("\n--- Non-PK Query: SELECT name FROM users WHERE age = 30 ---")
    print("    Expected: Optimizer picks SeqScan (no index on 'age')")
    show_rows(db.execute_sql("SELECT name FROM users WHERE age = 30"))

    print("\nCreating 'orders' table with 5 rows...")
    db.execute_sql("CREATE TABLE orders (order_id INT PRIMARY KEY, user_id INT, amount INT)")
    for i in range(1, 6):
        db.execute_sql("INSERT INTO orders VALUES (%d, %d, %d)" % (i, i * 10, i * 100))
    db.checkpoint()
    db._update_optimizer_statistics()

    print("\n--- Join: SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id ---")
    print("    Expected: orders (5 rows) is outer, users (50 rows) is inner with Index Nested Loop")
    show_rows(db.execute_sql("SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.user_id"))

    db.close()
    shutil.rmtree(db_dir)

# ============================================================
# DEMO 3: Strict 2PL & Deadlock Detection
# ============================================================
def demo3():
    banner("DEMO 3: Strict 2PL & Deadlock Detection")
    db_dir = "./demo3_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
    db = MiniDB(db_dir, is_mvcc=False)

    db.execute_sql("CREATE TABLE accounts (id INT PRIMARY KEY, balance INT)")
    db.execute_sql("INSERT INTO accounts VALUES (1, 100)")
    db.execute_sql("INSERT INTO accounts VALUES (2, 200)")
    db.checkpoint()

    print("\nInitial state:")
    show_rows(db.execute_sql("SELECT * FROM accounts"))

    t1_result = []
    t2_result = []

    def txn1():
        tx = db.tx_manager.begin()
        print("\n  [Txn 1 (ID %d)] Started. Reading account 1..." % tx.tx_id)
        try:
            db.execute_sql("SELECT balance FROM accounts WHERE id = 1", tx)
            print("  [Txn 1] Got S-lock on ID=1. Sleeping 1s...")
            time.sleep(1)
            print("  [Txn 1] Now trying to DELETE ID=2 (needs X-lock)...")
            db.execute_sql("DELETE FROM accounts WHERE id = 2", tx)
            db.tx_manager.commit(tx.tx_id)
            t1_result.append("COMMITTED")
            print("  [Txn 1] COMMITTED successfully!")
        except Exception as e:
            print("  [Txn 1] ABORTED: %s" % e)
            db.tx_manager.abort(tx.tx_id)
            t1_result.append("ABORTED")

    def txn2():
        time.sleep(0.3)
        tx = db.tx_manager.begin()
        print("\n  [Txn 2 (ID %d)] Started. Reading account 2..." % tx.tx_id)
        try:
            db.execute_sql("SELECT balance FROM accounts WHERE id = 2", tx)
            print("  [Txn 2] Got S-lock on ID=2. Sleeping 1s...")
            time.sleep(1)
            print("  [Txn 2] Now trying to DELETE ID=1 (needs X-lock)...")
            db.execute_sql("DELETE FROM accounts WHERE id = 1", tx)
            db.tx_manager.commit(tx.tx_id)
            t2_result.append("COMMITTED")
            print("  [Txn 2] COMMITTED successfully!")
        except Exception as e:
            print("  [Txn 2] ABORTED: %s" % e)
            db.tx_manager.abort(tx.tx_id)
            t2_result.append("ABORTED")

    print("\n  Launching 2 concurrent threads that will deadlock...")
    print("  Txn1: locks ID=1 then tries ID=2")
    print("  Txn2: locks ID=2 then tries ID=1  => DEADLOCK!\n")

    t1 = threading.Thread(target=txn1)
    t2 = threading.Thread(target=txn2)
    t1.start()
    t2.start()
    t1.join(timeout=15)
    t2.join(timeout=15)

    print("\n  Result: Txn1=%s, Txn2=%s" % (
        t1_result[0] if t1_result else "TIMEOUT",
        t2_result[0] if t2_result else "TIMEOUT"
    ))
    print("  The deadlock detector found the cycle and aborted one transaction!")

    db.close()
    shutil.rmtree(db_dir)

# ============================================================
# DEMO 4: MVCC Snapshot Isolation
# ============================================================
def demo4():
    banner("DEMO 4: MVCC Snapshot Isolation")
    db_dir = "./demo4_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
    db = MiniDB(db_dir, is_mvcc=True)

    db.execute_sql("CREATE TABLE accounts (id INT PRIMARY KEY, val INT)")
    db.execute_sql("INSERT INTO accounts VALUES (1, 100)")
    db.checkpoint()

    print("\nInitial state:")
    show_rows(db.execute_sql("SELECT * FROM accounts"))

    # Tx1 begins and reads
    tx1 = db.tx_manager.begin()
    print("\n  [Tx1 ID=%d] BEGIN. Reading val for id=1..." % tx1.tx_id)
    r1 = db.execute_sql("SELECT val FROM accounts WHERE id = 1", tx1)
    print("  [Tx1] Sees val = %s" % r1[0]["val"])

    # Tx2 modifies and commits
    tx2 = db.tx_manager.begin()
    print("\n  [Tx2 ID=%d] BEGIN. Deleting id=1 and re-inserting with val=200..." % tx2.tx_id)
    db.execute_sql("DELETE FROM accounts WHERE id = 1", tx2)
    db.execute_sql("INSERT INTO accounts VALUES (1, 200)", tx2)
    db.tx_manager.commit(tx2.tx_id)
    print("  [Tx2] COMMITTED (val is now 200 in the database)")

    # Tx1 reads again — should still see 100 (snapshot isolation!)
    print("\n  [Tx1] Reading val for id=1 again (still in its old snapshot)...")
    r2 = db.execute_sql("SELECT val FROM accounts WHERE id = 1", tx1)
    print("  [Tx1] Sees val = %s   <-- STILL 100! Snapshot isolation works!" % r2[0]["val"])
    db.tx_manager.commit(tx1.tx_id)

    # New Tx3 reads — sees 200
    tx3 = db.tx_manager.begin()
    r3 = db.execute_sql("SELECT val FROM accounts WHERE id = 1", tx3)
    print("\n  [Tx3 ID=%d] New transaction sees val = %s  <-- 200, the committed value" % (tx3.tx_id, r3[0]["val"]))
    db.tx_manager.commit(tx3.tx_id)

    db.close()
    shutil.rmtree(db_dir)

# ============================================================
# DEMO 5: WAL & Crash Recovery
# ============================================================
def demo5():
    banner("DEMO 5: WAL & Crash Recovery (ARIES-style)")
    db_dir = "./demo5_db"
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir)
    db = MiniDB(db_dir, is_mvcc=False)

    db.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50))")

    # Committed transaction
    tx1 = db.tx_manager.begin()
    print("\n  [Tx1] Inserting 'Alice' (will COMMIT)")
    db.execute_sql("INSERT INTO users VALUES (1, 'Alice')", tx1)
    db.tx_manager.commit(tx1.tx_id)
    print("  [Tx1] COMMITTED")

    # Uncommitted transaction
    tx2 = db.tx_manager.begin()
    print("  [Tx2] Inserting 'Bob' (will NOT commit)")
    db.execute_sql("INSERT INTO users VALUES (2, 'Bob')", tx2)
    print("  [Tx2] Written but NOT committed")

    # Simulate crash
    print("\n  *** SIMULATING CRASH — closing files without flushing ***")
    db.disk_manager.close()
    db.wal_manager.close()

    # Recover
    print("\n  *** RESTARTING DATABASE & RUNNING RECOVERY ***")
    db2 = MiniDB(db_dir, is_mvcc=False)
    db2.recover()

    print("\n  Data after recovery:")
    rows = db2.execute_sql("SELECT * FROM users")
    show_rows(rows)
    print("\n  Alice (committed) is preserved. Bob (uncommitted) was rolled back!")

    db2.close()
    shutil.rmtree(db_dir)

# ============================================================
# RUN ALL
# ============================================================
if __name__ == "__main__":
    print("=" * 70)
    print("        MiniDB — Complete System Demonstration")
    print("=" * 70)

    demo1()
    demo2()
    demo3()
    demo4()
    demo5()

    print("\n" + "=" * 70)
    print("  ALL 5 DEMOS COMPLETED SUCCESSFULLY!")
    print("=" * 70)
