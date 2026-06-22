from __future__ import annotations

import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.transactions import DeadlockError, TransactionAbortedError, TransactionManager


def main() -> None:
    txn_manager = TransactionManager()
    txn1 = txn_manager.begin()
    txn2 = txn_manager.begin()
    step_one = threading.Event()
    step_two = threading.Event()
    results: dict[str, str] = {}

    def worker_one() -> None:
        txn_manager.before_write(txn1, "demo:pk:A")
        print(f"T1 locked demo:pk:A")
        step_one.set()
        step_two.wait(timeout=1)
        try:
            txn_manager.before_write(txn1, "demo:pk:B")
            txn_manager.commit(txn1)
            results["T1"] = "COMMITTED"
        except (DeadlockError, TransactionAbortedError):
            results["T1"] = "ABORTED"

    def worker_two() -> None:
        step_one.wait(timeout=1)
        txn_manager.before_write(txn2, "demo:pk:B")
        print(f"T2 locked demo:pk:B")
        step_two.set()
        try:
            txn_manager.before_write(txn2, "demo:pk:A")
            txn_manager.commit(txn2)
            results["T2"] = "COMMITTED"
        except (DeadlockError, TransactionAbortedError):
            results["T2"] = "ABORTED"

    thread_one = threading.Thread(target=worker_one)
    thread_two = threading.Thread(target=worker_two)
    thread_one.start()
    thread_two.start()
    thread_one.join(timeout=2)
    thread_two.join(timeout=2)

    print("\nResults:")
    for txn_name in ("T1", "T2"):
        print(f"{txn_name}: {results.get(txn_name, 'UNKNOWN')}")

    print("\nLock manager log:")
    for line in txn_manager.lock_manager.logs:
        print(line)


if __name__ == "__main__":
    main()

