"""Demo: durability and crash recovery (WAL + ARIES redo/undo).

Commits one transaction and leaves another uncommitted, then *simulates a
crash* by abandoning the engine without a clean checkpoint (the log is forced
but dirty data pages are not flushed). On restart, recovery redoes the
committed transaction and undoes the uncommitted one.
"""

from _bootstrap import rule, scratch

from minidb.engine import Database


def main() -> None:
    path = scratch("recovery")

    rule("1. Session A: commit T1, leave T2 uncommitted, then 'crash'")
    db = Database(path)
    c = db.connect()
    c.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT)")
    c.execute("INSERT INTO accounts VALUES (1,'alice',100),(2,'bob',50)")
    print("T1 committed: inserted alice=100, bob=50")

    c.execute("BEGIN")
    c.execute("INSERT INTO accounts VALUES (3,'mallory',999)")
    c.execute("DELETE FROM accounts WHERE id = 1")
    print("T2 (uncommitted): inserted mallory=999, deleted alice")
    print("state seen inside T2:",
          sorted(r[0] for r in c.execute("SELECT id FROM accounts").rows))

    db.log.flush()          # log is durable...
    db.log.close()          # ...but we crash before checkpoint/flush of pages
    print("\n*** CRASH (no checkpoint, dirty pages lost) ***")

    rule("2. Session B: reopen -> recovery runs automatically")
    db2 = Database(path)
    print(db2.recovery_report.summary())

    rule("3. Post-recovery state")
    c2 = db2.connect()
    rows = c2.execute("SELECT id, owner, balance FROM accounts").rows
    for r in sorted(rows):
        print(r)
    ids = sorted(r[0] for r in rows)
    assert ids == [1, 2], f"expected committed-only state, got {ids}"
    print("\nCommitted T1 preserved; uncommitted T2 rolled back. Consistent.")
    db2.close()


if __name__ == "__main__":
    main()
