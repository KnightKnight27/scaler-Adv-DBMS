"""Demo: concurrency control — strict 2PL, lock acquisition, and deadlock.

Two transactions each lock one table then reach for the other in the opposite
order, forming a cycle. MiniDB's waits-for graph detects the deadlock and
raises on the transaction that would close the cycle; it aborts and the other
transaction makes progress.
"""

import threading
import time

from _bootstrap import rule, scratch

from minidb.engine import Database
from minidb.txn.lock_manager import DeadlockError


def main() -> None:
    db = Database(scratch("concurrency"))
    setup = db.connect()
    setup.execute("CREATE TABLE a (id INT PRIMARY KEY, v INT)")
    setup.execute("CREATE TABLE b (id INT PRIMARY KEY, v INT)")
    setup.execute("INSERT INTO a VALUES (1,10)")
    setup.execute("INSERT INTO b VALUES (1,20)")

    rule("Two concurrent transactions acquiring locks in opposite order")
    c1, c2 = db.connect(), db.connect()
    c1.execute("BEGIN"); c2.execute("BEGIN")

    # T1 writes A, T2 writes B (each takes an exclusive table lock).
    c1.execute("INSERT INTO a VALUES (2,11)")
    print("T1: locked table A (X)")
    c2.execute("INSERT INTO b VALUES (2,21)")
    print("T2: locked table B (X)")
    print("locks held:", db.lock_manager.snapshot())

    outcome = {}

    def t1():
        try:
            c1.execute("INSERT INTO b VALUES (3,12)")  # wants B -> waits on T2
            outcome["t1"] = "committed"
            c1.execute("COMMIT")
        except DeadlockError:
            outcome["t1"] = "deadlock-aborted"
            c1.execute("ROLLBACK")

    def t2():
        time.sleep(0.2)                                # ensure T1 is waiting
        try:
            c2.execute("INSERT INTO a VALUES (3,22)")  # wants A -> closes cycle
            outcome["t2"] = "committed"
            c2.execute("COMMIT")
        except DeadlockError:
            outcome["t2"] = "deadlock-aborted"
            c2.execute("ROLLBACK")

    th1 = threading.Thread(target=t1)
    th2 = threading.Thread(target=t2)
    th1.start(); th2.start()
    th1.join(); th2.join()

    rule("Outcome")
    print("T1:", outcome.get("t1"))
    print("T2:", outcome.get("t2"))
    victims = [k for k, v in outcome.items() if v == "deadlock-aborted"]
    print(f"\nDeadlock detected and resolved by aborting: {victims}")

    # The surviving transaction's effects are visible; both tables consistent.
    rc = db.connect()
    print("table a:", sorted(r[0] for r in rc.execute("SELECT id FROM a").rows))
    print("table b:", sorted(r[0] for r in rc.execute("SELECT id FROM b").rows))
    db.close()


if __name__ == "__main__":
    main()
