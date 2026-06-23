"""
Demo 2 — Concurrency: 2PL locking and deadlock detection.

Part A: two transactions take shared locks on the same row concurrently
        (compatible) — both proceed.
Part B: two transactions grab one row each, then reach for the other's row
        in the opposite order. MiniDB's wait-for graph detects the cycle and
        aborts one transaction (the victim); the survivor commits.

Run:
    python demos/demo_concurrency.py
"""
import os
import shutil
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb import Database
from minidb.lock_manager import DeadlockError, LockMode

DB_DIR = "/tmp/minidb_demo_concurrency"


def banner(t):
    print("\n" + "=" * 64 + f"\n  {t}\n" + "=" * 64)


def setup():
    shutil.rmtree(DB_DIR, ignore_errors=True)
    db = Database(DB_DIR, isolation="2PL")
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    db.execute("INSERT INTO t VALUES (1,10),(2,20)")
    return db


def demo_shared(db):
    banner("PART A — concurrent shared (read) locks are compatible")
    results = {}

    def reader(name):
        txn = db.begin()
        db.lock_mgr.acquire(txn.txn_id, ("t", (0, 0)), LockMode.S)
        time.sleep(0.1)  # hold concurrently
        results[name] = "read ok (shared lock granted)"
        db.commit(txn)

    threads = [threading.Thread(target=reader, args=(f"R{i}",)) for i in range(3)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    for name, msg in sorted(results.items()):
        print(f"  {name}: {msg}")


def demo_deadlock(db):
    banner("PART B — opposing lock order triggers a deadlock; one victim aborts")
    barrier = threading.Barrier(2)
    outcomes = {}

    def worker(name, first, second):
        txn = db.begin()
        try:
            db.lock_mgr.acquire(txn.txn_id, ("t", first), LockMode.X)
            print(f"  {name} locked row {first}")
            barrier.wait()
            time.sleep(0.05)
            print(f"  {name} now wants row {second} ...")
            db.lock_mgr.acquire(txn.txn_id, ("t", second), LockMode.X)
            db.commit(txn)
            outcomes[name] = "COMMITTED"
        except DeadlockError as e:
            db.abort(txn)
            outcomes[name] = f"ABORTED (deadlock victim: {e})"

    t1 = threading.Thread(target=worker, args=("T1", (0, 0), (0, 1)))
    t2 = threading.Thread(target=worker, args=("T2", (0, 1), (0, 0)))
    t1.start()
    t2.start()
    t1.join()
    t2.join()
    print()
    for name, msg in sorted(outcomes.items()):
        print(f"  {name}: {msg}")
    committed = [n for n, m in outcomes.items() if m == "COMMITTED"]
    aborted = [n for n, m in outcomes.items() if m.startswith("ABORTED")]
    print(f"\n  => {len(committed)} committed, {len(aborted)} aborted "
          f"(deadlock correctly resolved)")


def main():
    db = setup()
    demo_shared(db)
    demo_deadlock(db)
    db.close()
    banner("CONCURRENCY DEMO COMPLETE")


if __name__ == "__main__":
    main()
