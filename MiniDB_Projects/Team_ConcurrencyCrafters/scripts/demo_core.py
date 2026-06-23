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
    runtime_dir = ROOT / "data" / "demo_core"
    shutil.rmtree(runtime_dir, ignore_errors=True)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    db = MiniDBEngine(runtime_dir)

    print("Core MiniDB demo")
    print(db.execute("CREATE TABLE accounts (id INT, name TEXT, balance INT);"))
    print(db.execute("CREATE TABLE transactions (id INT, account_id INT, amount INT);"))
    print(db.execute("CREATE INDEX idx_accounts_id ON accounts(id);"))

    print(db.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);"))
    print(db.execute("INSERT INTO accounts VALUES (2, 'Bina', 750);"))
    print(db.execute("INSERT INTO transactions VALUES (101, 1, 200);"))
    print(db.execute("INSERT INTO transactions VALUES (102, 2, 125);"))

    print(db.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;"))
    print(db.execute("SELECT * FROM accounts WHERE id = 1;"))
    print(db.execute("SELECT COUNT(*) FROM accounts;"))
    print(db.execute("SELECT * FROM accounts WHERE id = 1 AND balance = 1000;"))
    print(db.execute("EXPLAIN SELECT * FROM accounts WHERE id >= 1 AND id <= 2;"))
    print(db.execute("SELECT * FROM accounts WHERE id >= 1 AND id <= 2;"))
    print(
        db.execute(
            "SELECT * FROM accounts JOIN transactions ON accounts.id = transactions.account_id;"
        )
    )

    print(db.execute("SET MODE 2PL;"))
    print(db.execute("BEGIN;"))
    print(db.execute("DELETE FROM accounts WHERE id = 2;"))
    print(db.execute("ROLLBACK;"))
    print(db.execute("SELECT * FROM accounts WHERE id = 2;"))

    print(db.execute("SET MODE MVCC;"))
    print(db.execute("BEGIN;"))
    print(db.execute("DELETE FROM accounts WHERE id = 1;"))
    print(db.execute("INSERT INTO accounts VALUES (1, 'Asha', 900);"))
    print(db.execute("COMMIT;"))
    print(db.execute("SELECT * FROM accounts WHERE id = 1;"))
    print(db.vacuum())


if __name__ == "__main__":
    main()
