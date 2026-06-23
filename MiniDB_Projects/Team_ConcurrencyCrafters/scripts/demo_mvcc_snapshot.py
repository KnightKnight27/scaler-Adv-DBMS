from __future__ import annotations

import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.engine import MiniDBEngine


def main() -> None:
    runtime_dir = ROOT / "data" / "demo_mvcc_snapshot"
    shutil.rmtree(runtime_dir, ignore_errors=True)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    db = MiniDBEngine(runtime_dir)
    db.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);")
    db.execute("SET MODE MVCC;")
    db.execute("INSERT INTO accounts VALUES (1, 'A', 1000);")

    t1 = db.begin_transaction()
    first_read = db.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t1)

    t2 = db.begin_transaction()
    db.execute("DELETE FROM accounts WHERE id = 1;", txn_id=t2)
    db.execute("INSERT INTO accounts VALUES (1, 'A', 900);", txn_id=t2)
    db.commit_transaction(t2)

    second_read = db.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t1)
    t3 = db.begin_transaction()
    newest_read = db.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t3)

    print("Scenario A - MVCC snapshot isolation")
    print(f"T1 first read:  {first_read}")
    print(f"T1 second read: {second_read}")
    print(f"New transaction read after T2 commit: {newest_read}")


if __name__ == "__main__":
    main()

