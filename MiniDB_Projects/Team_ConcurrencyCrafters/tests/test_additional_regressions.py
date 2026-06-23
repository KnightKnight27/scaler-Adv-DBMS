from __future__ import annotations

import shutil
import sys
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.engine import MiniDBEngine
from minidb.transactions import (
    DeadlockError,
    TransactionAbortedError,
    TransactionManager,
)

TEST_RUNTIME_ROOT = ROOT / ".test_runtime_additional"
TEST_RUNTIME_ROOT.mkdir(exist_ok=True)


def make_runtime_dir(name: str) -> Path:
    path = TEST_RUNTIME_ROOT / name
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True, exist_ok=True)
    return path


class ExplainAndModeRegressionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_dir = make_runtime_dir(self._testMethodName)
        self.engine = MiniDBEngine(self.runtime_dir)
        self.engine.execute(
            "CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);"
        )
        self.engine.execute(
            "CREATE TABLE transactions (id INT PRIMARY KEY, account_id INT, amount INT);"
        )

    def tearDown(self) -> None:
        shutil.rmtree(self.runtime_dir, ignore_errors=True)

    def seed_join_rows(self) -> list[dict[str, int | str]]:
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        self.engine.execute("INSERT INTO accounts VALUES (2, 'Bina', 750);")
        self.engine.execute("INSERT INTO transactions VALUES (101, 1, 200);")
        self.engine.execute("INSERT INTO transactions VALUES (102, 2, 125);")
        return [
            {
                "accounts.id": 1,
                "accounts.owner": "Asha",
                "accounts.balance": 1000,
                "transactions.id": 101,
                "transactions.account_id": 1,
                "transactions.amount": 200,
            },
            {
                "accounts.id": 2,
                "accounts.owner": "Bina",
                "accounts.balance": 750,
                "transactions.id": 102,
                "transactions.account_id": 2,
                "transactions.amount": 125,
            },
        ]

    def test_explain_shows_index_scan_and_2pl_mode(self) -> None:
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")

        explain = self.engine.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;")

        self.assertIn("INDEX_SCAN", explain)
        self.assertIn("transaction_mode=2PL_LOCKING", explain)

    def test_explain_shows_mvcc_snapshot_after_set_mode_mvcc(self) -> None:
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        self.engine.execute("SET MODE MVCC;")

        explain = self.engine.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;")

        self.assertIn("INDEX_SCAN", explain)
        self.assertIn("transaction_mode=MVCC_SNAPSHOT", explain)

    def test_select_where_primary_key_uses_btree_path_and_applies_mvcc_visibility(self) -> None:
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        self.engine.execute("SET MODE MVCC;")

        explain = self.engine.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;")
        self.assertIn("INDEX_SCAN", explain)
        self.assertIn("transaction_mode=MVCC_SNAPSHOT", explain)

        snapshot_reader = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=snapshot_reader,
            ),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )

        writer = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=writer)
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 900);", txn_id=writer)
        self.engine.commit_transaction(writer)

        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=snapshot_reader,
            ),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )

        fresh_reader = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=fresh_reader,
            ),
            [{"id": 1, "owner": "Asha", "balance": 900}],
        )
        self.engine.commit_transaction(snapshot_reader)
        self.engine.commit_transaction(fresh_reader)

    def test_join_still_works_after_switching_transaction_modes(self) -> None:
        expected_rows = self.seed_join_rows()
        join_sql = (
            "SELECT * FROM accounts JOIN transactions ON accounts.id = transactions.account_id;"
        )

        self.assertEqual(self.engine.execute(join_sql), expected_rows)

        self.engine.execute("SET MODE MVCC;")
        self.assertEqual(self.engine.execute(join_sql), expected_rows)

        self.engine.execute("SET MODE 2PL;")
        self.assertEqual(self.engine.execute(join_sql), expected_rows)


class RecoveryRegressionTests(unittest.TestCase):
    def test_wal_recovery_keeps_committed_insert_after_simulated_restart(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);")

        committed_txn = engine.begin_transaction()
        engine.execute(
            "INSERT INTO accounts VALUES (1, 'Asha', 1000);",
            txn_id=committed_txn,
        )
        engine.commit_transaction(committed_txn)

        restarted = MiniDBEngine(runtime_dir)
        self.assertEqual(
            restarted.execute("SELECT * FROM accounts WHERE id = 1;"),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )

    def test_wal_recovery_ignores_uncommitted_insert_after_simulated_crash(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);")

        committed_txn = engine.begin_transaction()
        engine.execute(
            "INSERT INTO accounts VALUES (1, 'Asha', 1000);",
            txn_id=committed_txn,
        )
        engine.commit_transaction(committed_txn)

        crashed_txn = engine.begin_transaction()
        engine.execute(
            "INSERT INTO accounts VALUES (2, 'Bina', 750);",
            txn_id=crashed_txn,
        )

        restarted = MiniDBEngine(runtime_dir)
        self.assertEqual(
            restarted.execute("SELECT * FROM accounts;"),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )


class MVCCDeleteTombstoneRegressionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_dir = make_runtime_dir(self._testMethodName)
        self.engine = MiniDBEngine(self.runtime_dir)
        self.engine.execute(
            "CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);"
        )
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        self.engine.execute("SET MODE MVCC;")

    def tearDown(self) -> None:
        shutil.rmtree(self.runtime_dir, ignore_errors=True)

    def test_mvcc_delete_tombstone_hides_row_only_for_snapshots_that_see_delete(self) -> None:
        reader_before_delete = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=reader_before_delete,
            ),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )

        deleter = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=deleter)
        self.engine.commit_transaction(deleter)

        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=reader_before_delete,
            ),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )

        reader_after_delete = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=reader_after_delete,
            ),
            [],
        )
        self.engine.commit_transaction(reader_before_delete)
        self.engine.commit_transaction(reader_after_delete)


class DeadlockDemoRegressionTests(unittest.TestCase):
    def test_2pl_deadlock_demo_behavior_remains_deterministic(self) -> None:
        txn_manager = TransactionManager()
        txn1 = txn_manager.begin()
        txn2 = txn_manager.begin()
        step_one = threading.Event()
        step_two = threading.Event()
        results: dict[str, str] = {}

        def worker_one() -> None:
            txn_manager.before_write(txn1, "demo:pk:A")
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

        self.assertEqual(results.get("T1"), "COMMITTED")
        self.assertEqual(results.get("T2"), "ABORTED")
        logs = "\n".join(txn_manager.lock_manager.logs)
        self.assertIn(
            f"lock acquired txn={txn1} resource=demo:pk:A mode=EXCLUSIVE",
            logs,
        )
        self.assertIn(
            f"lock acquired txn={txn2} resource=demo:pk:B mode=EXCLUSIVE",
            logs,
        )
        self.assertIn(f"victim={txn2}", logs)


if __name__ == "__main__":
    unittest.main()
