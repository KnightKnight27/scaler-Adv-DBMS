"""
Demo 1 — Crash recovery (WAL, redo + undo).

Shows that after a simulated crash:
  * committed transactions survive (durability / redo)
  * an uncommitted transaction whose dirty pages reached disk (steal) is
    rolled back (atomicity / undo)

Run:
    python demos/demo_recovery.py
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb import Database

DB_DIR = "/tmp/minidb_demo_recovery"


def banner(t):
    print("\n" + "=" * 64 + f"\n  {t}\n" + "=" * 64)


def main():
    shutil.rmtree(DB_DIR, ignore_errors=True)

    banner("SESSION 1 — do work, then crash")
    db = Database(DB_DIR, isolation="2PL")
    db.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT)")
    db.execute("INSERT INTO accounts VALUES (1,'alice',1000),(2,'bob',500)")
    print("committed two accounts (autocommit).")

    # an explicit transaction we will NOT commit
    bad = db.begin()
    db.execute("INSERT INTO accounts VALUES (3,'mallory',999999)", txn=bad)
    db.execute("DELETE FROM accounts WHERE id = 1", txn=bad)
    print("uncommitted txn inserted id=3 and deleted id=1.")

    # force the uncommitted dirty pages to disk (STEAL policy)
    db.bufferpool.flush_all()
    db.disk.fsync_all()
    db.wal.flush()
    print("STEAL: uncommitted pages forced to disk. Raw heap now holds:")
    for _rid, raw in db.heaps["accounts"].scan():
        print("   ", db.catalog.get("accounts").schema.deserialize(raw))

    # CRASH: abandon the process state without committing/closing
    db.disk.close()
    del db, bad
    print("\n>>> CRASH <<<  (process state discarded, no COMMIT for the bad txn)")

    banner("SESSION 2 — reopen, recovery runs automatically")
    db = Database(DB_DIR, isolation="2PL")
    print("recovery report:", db.last_recovery)
    print("\nfinal state — alice & bob survive, mallory rolled back, id=1 restored:")
    print(db.execute("SELECT id, owner, balance FROM accounts"))

    # prove indexes are consistent post-recovery
    print("\nindex lookup id=1 (restored):",
          list(db.execute("SELECT owner FROM accounts WHERE id = 1")))
    print("index lookup id=3 (rolled back):",
          list(db.execute("SELECT owner FROM accounts WHERE id = 3")))
    db.close()
    banner("RECOVERY DEMO COMPLETE")


if __name__ == "__main__":
    main()
