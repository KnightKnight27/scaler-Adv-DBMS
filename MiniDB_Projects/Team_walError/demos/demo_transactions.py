"""demo_transactions.py — concurrent 2PL transactions and a resolved deadlock."""

import threading
import time

import _demo
from _demo import banner, show, step

from minidb.lock_manager import DeadlockError, LockMode
from minidb.transaction import TransactionManager

S, X = LockMode.SHARED, LockMode.EXCLUSIVE


def main() -> None:
    banner("TRANSACTIONS: strict 2PL, shared/exclusive locks")
    tm = TransactionManager()

    step("Two transactions can hold SHARED locks on the same row together")
    r1, r2 = tm.begin(), tm.begin()
    r1.lock_shared("accounts:1")
    r2.lock_shared("accounts:1")
    show("shared holders of accounts:1", set(tm.lock_manager.holders("accounts:1")))
    tm.commit(r1); tm.commit(r2)

    banner("DEADLOCK: detected via the wait-for graph and resolved")
    print("  T1 locks A then wants B;  T2 locks B then wants A  -> a cycle.")
    tm2 = TransactionManager()
    t1, t2 = tm2.begin(), tm2.begin()
    t1.lock_exclusive("A")
    t2.lock_exclusive("B")
    show("T1 holds", set(t1.locks))
    show("T2 holds", set(t2.locks))

    outcomes: dict[int, str] = {}
    ready = threading.Barrier(2)

    def worker(txn, want):
        ready.wait()
        try:
            txn.lock_exclusive(want)
            outcomes[txn.txn_id] = f"acquired {want}"
            tm2.commit(txn)
        except DeadlockError as e:
            outcomes[txn.txn_id] = "ABORTED (deadlock victim)"
            tm2.abort(txn)
            print(f"      detected: {e}")

    th1 = threading.Thread(target=worker, args=(t1, "B"))
    th2 = threading.Thread(target=worker, args=(t2, "A"))
    th1.start(); th2.start()
    th1.join(3); th2.join(3)

    step("Outcome: exactly one transaction is the victim, the other proceeds")
    for tid, res in sorted(outcomes.items()):
        show(f"T{tid}", res)
    victims = [r for r in outcomes.values() if "victim" in r]
    assert len(victims) == 1, f"expected exactly one victim, got {outcomes}"
    print("\nTakeaway: locks are held until end-of-transaction (strict 2PL); a")
    print("wait-for cycle is detected at wait time and broken by aborting a victim.")


if __name__ == "__main__":
    main()
