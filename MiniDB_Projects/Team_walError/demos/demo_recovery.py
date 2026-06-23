"""demo_recovery.py — the crown jewel: crash -> recover via WAL.

Committed transactions survive a crash; uncommitted ones vanish.
"""

import os
import tempfile

import _demo
from _demo import banner, sql, step, show

from minidb import Database


def main() -> None:
    banner("RECOVERY: Write-Ahead Log + redo (committed survive, uncommitted vanish)")
    tmpdir = tempfile.mkdtemp(prefix="minidb_recovery_")
    path = os.path.join(tmpdir, "bank.db")

    step("Session 1: create a table and COMMIT one transaction")
    db = Database(path)
    sql(db, "CREATE TABLE accounts (id INT PRIMARY KEY, balance INT)")
    sql(db, "BEGIN")
    sql(db, "INSERT INTO accounts VALUES (1, 100), (2, 200)")
    sql(db, "COMMIT")

    step("Start a SECOND transaction but do NOT commit it")
    sql(db, "BEGIN")
    sql(db, "INSERT INTO accounts VALUES (3, 999)")
    print("       (no COMMIT — this work is in-flight)")

    step("CRASH! (drop the process without flushing or committing)")
    db.crash()
    show("on-disk WAL exists", os.path.exists(path + ".wal"))

    step("Session 2: reopen -> recovery replays the WAL")
    db2 = Database(path)
    show("recovery stats", db2.recovery_stats)
    sql(db2, "SELECT id, balance FROM accounts")

    step("Verify the durability guarantee")
    ids = {row[0] for row in db2.execute("SELECT id FROM accounts").rows}
    show("committed rows (1,2) survived", {1, 2} <= ids)
    show("uncommitted row (3) vanished", 3 not in ids)
    assert {1, 2} <= ids and 3 not in ids, "recovery guarantee violated!"
    db2.close()

    print("\nTakeaway: the WAL is the source of truth. On restart MiniDB replays only")
    print("transactions that logged a COMMIT, so a crash can never expose partial work.")


if __name__ == "__main__":
    main()
