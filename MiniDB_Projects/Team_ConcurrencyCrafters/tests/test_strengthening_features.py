from __future__ import annotations

import shutil
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.engine import MiniDBEngine

TEST_RUNTIME_ROOT = ROOT / ".test_runtime_strengthening"
TEST_RUNTIME_ROOT.mkdir(exist_ok=True)


def make_runtime_dir(name: str) -> Path:
    path = TEST_RUNTIME_ROOT / name
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True, exist_ok=True)
    return path


class SQLBreadthStrengtheningTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_dir = make_runtime_dir(self._testMethodName)
        self.engine = MiniDBEngine(self.runtime_dir)
        self.engine.execute(
            "CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);"
        )
        self.engine.execute(
            "CREATE TABLE transactions (id INT PRIMARY KEY, account_id INT, amount INT);"
        )
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        self.engine.execute("INSERT INTO accounts VALUES (2, 'Bina', 750);")
        self.engine.execute("INSERT INTO accounts VALUES (3, 'Cora', 1000);")
        self.engine.execute("INSERT INTO transactions VALUES (101, 1, 200);")
        self.engine.execute("INSERT INTO transactions VALUES (102, 2, 125);")

    def tearDown(self) -> None:
        shutil.rmtree(self.runtime_dir, ignore_errors=True)

    def test_count_star_works_in_2pl_mode(self) -> None:
        self.assertEqual(
            self.engine.execute("SELECT COUNT(*) FROM accounts;"),
            [{"count": 3}],
        )
        self.assertEqual(
            self.engine.execute("SELECT COUNT(*) FROM accounts WHERE balance = 1000;"),
            [{"count": 2}],
        )
        self.assertEqual(
            self.engine.execute(
                "SELECT COUNT(*) FROM accounts JOIN transactions ON accounts.id = transactions.account_id;"
            ),
            [{"count": 2}],
        )

    def test_count_star_works_in_mvcc_mode(self) -> None:
        self.engine.execute("SET MODE MVCC;")
        self.assertEqual(
            self.engine.execute("SELECT COUNT(*) FROM accounts;"),
            [{"count": 3}],
        )
        self.assertEqual(
            self.engine.execute("SELECT COUNT(*) FROM accounts WHERE id = 1 AND balance = 1000;"),
            [{"count": 1}],
        )

    def test_and_predicate_works_in_2pl_mode(self) -> None:
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1 AND balance = 1000;"),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )
        self.assertEqual(
            self.engine.execute("SELECT COUNT(*) FROM accounts WHERE id = 1 AND balance = 1000;"),
            [{"count": 1}],
        )
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1 AND balance = 750;"),
            [],
        )

    def test_and_predicate_works_in_mvcc_mode(self) -> None:
        self.engine.execute("SET MODE MVCC;")
        reader_txn = self.engine.begin_transaction()
        writer_txn = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=writer_txn)
        self.engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 900);", txn_id=writer_txn)
        self.engine.commit_transaction(writer_txn)

        self.assertEqual(
            self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1 AND balance = 1000;",
                txn_id=reader_txn,
            ),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )
        self.assertEqual(
            self.engine.execute(
                "SELECT COUNT(*) FROM accounts WHERE id = 1 AND balance = 1000;",
                txn_id=reader_txn,
            ),
            [{"count": 1}],
        )

    def test_explain_shows_count_aggregate_plan(self) -> None:
        explain = self.engine.execute("EXPLAIN SELECT COUNT(*) FROM accounts WHERE id = 1;")

        self.assertIn("INDEX_SCAN", explain)
        self.assertIn("aggregate=COUNT(*)", explain)

    def test_explain_shows_index_range_scan_when_range_is_used(self) -> None:
        explain = self.engine.execute(
            "EXPLAIN SELECT * FROM accounts WHERE id >= 1 AND id <= 2;"
        )
        rows = self.engine.execute(
            "SELECT * FROM accounts WHERE id >= 1 AND id <= 2;"
        )

        self.assertIn("INDEX_RANGE_SCAN", explain)
        self.assertEqual(
            rows,
            [
                {"id": 1, "owner": "Asha", "balance": 1000},
                {"id": 2, "owner": "Bina", "balance": 750},
            ],
        )


class MVCCVacuumStrengtheningTests(unittest.TestCase):
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

    def apply_update(self, balance: int) -> None:
        txn_id = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=txn_id)
        self.engine.execute(
            f"INSERT INTO accounts VALUES (1, 'Asha', {balance});",
            txn_id=txn_id,
        )
        self.engine.commit_transaction(txn_id)

    def test_old_versions_remain_visible_to_active_old_snapshot_before_vacuum(self) -> None:
        old_snapshot = self.engine.begin_transaction()
        self.apply_update(900)
        self.apply_update(800)

        report = self.engine.vacuum()

        self.assertGreater(int(report["removed_versions"]), 0)
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=old_snapshot),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )
        fresh_reader = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=fresh_reader),
            [{"id": 1, "owner": "Asha", "balance": 800}],
        )

    def test_after_old_transaction_ends_vacuum_removes_obsolete_versions(self) -> None:
        old_snapshot = self.engine.begin_transaction()
        self.apply_update(900)
        self.apply_update(800)

        before = self.engine.mvcc_manager.version_count("accounts")
        during_old_snapshot = self.engine.vacuum()
        middle = self.engine.mvcc_manager.version_count("accounts")
        self.engine.commit_transaction(old_snapshot)
        after_old_snapshot = self.engine.vacuum()
        after = self.engine.mvcc_manager.version_count("accounts")

        self.assertGreater(before, middle)
        self.assertGreater(middle, after)
        self.assertGreater(int(during_old_snapshot["removed_versions"]), 0)
        self.assertGreater(int(after_old_snapshot["removed_versions"]), 0)

    def test_latest_committed_version_remains_visible_after_vacuum(self) -> None:
        self.apply_update(900)
        self.apply_update(800)

        report = self.engine.vacuum()

        self.assertGreaterEqual(int(report["removed_versions"]), 1)
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;"),
            [{"id": 1, "owner": "Asha", "balance": 800}],
        )

    def test_tombstone_cleanup_does_not_resurrect_deleted_rows(self) -> None:
        txn_id = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=txn_id)
        self.engine.commit_transaction(txn_id)

        report = self.engine.vacuum()

        self.assertGreaterEqual(int(report["retained_versions"]), 1)
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;"),
            [],
        )


class RecoveryStrengtheningTests(unittest.TestCase):
    def test_committed_delete_survives_restart(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute(
            "CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);"
        )
        engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        tx1 = engine.begin_transaction()
        engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=tx1)
        engine.commit_transaction(tx1)

        restarted = MiniDBEngine(runtime_dir)

        self.assertEqual(
            restarted.execute("SELECT * FROM accounts WHERE id = 1;"),
            [],
        )

    def test_uncommitted_delete_is_ignored_after_restart(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute(
            "CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);"
        )
        engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        tx1 = engine.begin_transaction()
        engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=tx1)

        restarted = MiniDBEngine(runtime_dir)

        self.assertEqual(
            restarted.execute("SELECT * FROM accounts WHERE id = 1;"),
            [{"id": 1, "owner": "Asha", "balance": 1000}],
        )

    def test_recovery_report_includes_redone_and_ignored_operations(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute(
            "CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);"
        )
        committed_txn = engine.begin_transaction()
        engine.execute(
            "INSERT INTO accounts VALUES (1, 'Asha', 1000);",
            txn_id=committed_txn,
        )
        engine.commit_transaction(committed_txn)

        uncommitted_txn = engine.begin_transaction()
        engine.execute(
            "INSERT INTO accounts VALUES (2, 'Bina', 750);",
            txn_id=uncommitted_txn,
        )

        restarted = MiniDBEngine(runtime_dir)
        report = restarted.recover()

        self.assertEqual(report["committed_transactions"], [1])
        self.assertEqual(report["uncommitted_transactions"], [2])
        self.assertGreaterEqual(int(report["redone_operations"]), 1)
        self.assertGreaterEqual(int(report["undone_or_ignored_operations"]), 1)
        self.assertEqual(int(report["committed_txn_count"]), 1)
        self.assertEqual(int(report["uncommitted_txn_count"]), 1)


if __name__ == "__main__":
    unittest.main()
