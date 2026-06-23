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
    runtime_dir = ROOT / "data" / "demo_recovery"
    shutil.rmtree(runtime_dir, ignore_errors=True)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    db = MiniDBEngine(runtime_dir)
    db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")

    committed_txn = db.begin_transaction()
    db.execute("INSERT INTO users VALUES (1, 'Ada', 31);", txn_id=committed_txn)
    db.commit_transaction(committed_txn)

    uncommitted_txn = db.begin_transaction()
    db.execute("INSERT INTO users VALUES (2, 'Bob', 28);", txn_id=uncommitted_txn)

    recovered = MiniDBEngine(runtime_dir)
    rows = recovered.execute("SELECT * FROM users;")

    print("Crash recovery demo")
    print("Committed transaction inserted id=1 and committed.")
    print("Uncommitted transaction inserted id=2 and crashed before commit.")
    print(f"Rows after recovery: {rows}")
    print(f"Recovery summary: {recovered.recover()}")


if __name__ == "__main__":
    main()

