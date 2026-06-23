from __future__ import annotations

import shutil
import sys
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from minidb.engine import MiniDBEngine
from minidb.types import TransactionMode

TEST_RUNTIME_ROOT = ROOT / ".test_runtime_track_b"
TEST_RUNTIME_ROOT.mkdir(exist_ok=True)


def make_runtime_dir(name: str) -> Path:
    path = TEST_RUNTIME_ROOT / name
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True, exist_ok=True)
    return path


class WALAndRecoveryTests(unittest.TestCase):
    def test_wal_record_creation(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")
        engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        records = engine.wal_manager.read_records()
        record_types = [record["type"] for record in records]
        self.assertIn("BEGIN", record_types)
        self.assertIn("INSERT", record_types)
        self.assertIn("COMMIT", record_types)

    def test_wal_flush_before_page_flush(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")
        engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        flush_index = next(
            index
            for index, event in enumerate(engine.wal_manager.events)
            if event.startswith("wal flush")
        )
        page_flush_index = next(
            index
            for index, event in enumerate(engine.wal_manager.events)
            if event.startswith("wal durable before page flush")
        )
        self.assertLess(flush_index, page_flush_index)

    def test_recovery_keeps_committed_transaction(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")
        tx1 = engine.begin_transaction()
        engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);", txn_id=tx1)
        engine.commit_transaction(tx1)
        restarted = MiniDBEngine(runtime_dir)
        self.assertEqual(
            restarted.execute("SELECT * FROM users;"),
            [{"id": 1, "name": "Ada", "age": 31}],
        )

    def test_recovery_removes_uncommitted_transaction(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)
        engine.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")
        tx1 = engine.begin_transaction()
        engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);", txn_id=tx1)
        engine.commit_transaction(tx1)
        tx2 = engine.begin_transaction()
        engine.execute("INSERT INTO users VALUES (2, 'Bob', 28);", txn_id=tx2)
        restarted = MiniDBEngine(runtime_dir)
        self.assertEqual(
            restarted.execute("SELECT * FROM users;"),
            [{"id": 1, "name": "Ada", "age": 31}],
        )


class MVCCTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_dir = make_runtime_dir(self._testMethodName)
        self.engine = MiniDBEngine(self.runtime_dir)
        self.engine.execute("CREATE TABLE accounts (id INT PRIMARY KEY, owner TEXT, balance INT);")
        self.engine.execute("SET MODE MVCC;")

    def tearDown(self) -> None:
        shutil.rmtree(self.runtime_dir, ignore_errors=True)

    def seed_account(self, balance: int = 1000) -> None:
        self.engine.execute(f"INSERT INTO accounts VALUES (1, 'A', {balance});")

    def test_mvcc_snapshot_read(self) -> None:
        self.seed_account(1000)
        self.assertIn(
            "transaction_mode=MVCC_SNAPSHOT",
            self.engine.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;"),
        )
        t1 = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t1),
            [{"id": 1, "owner": "A", "balance": 1000}],
        )
        t2 = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=t2)
        self.engine.execute("INSERT INTO accounts VALUES (1, 'A', 900);", txn_id=t2)
        self.engine.commit_transaction(t2)
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t1),
            [{"id": 1, "owner": "A", "balance": 1000}],
        )

    def test_mvcc_own_write_visibility(self) -> None:
        t1 = self.engine.begin_transaction()
        self.engine.execute("INSERT INTO accounts VALUES (10, 'B', 250);", txn_id=t1)
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 10;", txn_id=t1),
            [{"id": 10, "owner": "B", "balance": 250}],
        )

    def test_mvcc_hides_other_uncommitted_writes(self) -> None:
        t1 = self.engine.begin_transaction()
        self.engine.execute("INSERT INTO accounts VALUES (10, 'B', 250);", txn_id=t1)
        t2 = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 10;", txn_id=t2),
            [],
        )

    def test_mvcc_rollback_discards_versions(self) -> None:
        t1 = self.engine.begin_transaction()
        self.engine.execute("INSERT INTO accounts VALUES (10, 'B', 250);", txn_id=t1)
        self.engine.rollback_transaction(t1)
        t2 = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 10;", txn_id=t2),
            [],
        )

    def test_mvcc_delete_tombstone_behavior(self) -> None:
        self.seed_account(1000)
        t1 = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=t1)
        self.engine.commit_transaction(t1)
        t2 = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t2),
            [],
        )

    def test_mvcc_newest_visible_version_selection(self) -> None:
        self.seed_account(1000)
        t1 = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=t1)
        self.engine.execute("INSERT INTO accounts VALUES (1, 'A', 900);", txn_id=t1)
        self.engine.commit_transaction(t1)
        t2 = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=t2)
        self.engine.execute("INSERT INTO accounts VALUES (1, 'A', 800);", txn_id=t2)
        self.engine.commit_transaction(t2)
        t3 = self.engine.begin_transaction()
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;", txn_id=t3),
            [{"id": 1, "owner": "A", "balance": 800}],
        )

    def test_mvcc_readers_do_not_block_writers(self) -> None:
        self.seed_account(1000)
        writer_txn = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=writer_txn)
        result: dict[str, object] = {}
        finished = threading.Event()

        def reader() -> None:
            reader_txn = self.engine.begin_transaction()
            start = time.perf_counter()
            result["rows"] = self.engine.execute(
                "SELECT * FROM accounts WHERE id = 1;",
                txn_id=reader_txn,
            )
            result["elapsed"] = time.perf_counter() - start
            self.engine.commit_transaction(reader_txn)
            finished.set()

        thread = threading.Thread(target=reader)
        thread.start()
        thread.join(timeout=0.25)
        self.assertTrue(finished.is_set())
        self.assertEqual(result["rows"], [{"id": 1, "owner": "A", "balance": 1000}])
        self.assertLess(float(result["elapsed"]), 0.2)
        self.engine.rollback_transaction(writer_txn)

    def test_2pl_mode_still_works_after_mvcc_addition(self) -> None:
        self.seed_account(1000)
        mvcc_txn = self.engine.begin_transaction()
        self.engine.execute("DELETE FROM accounts WHERE id = 1;", txn_id=mvcc_txn)
        self.engine.execute("INSERT INTO accounts VALUES (1, 'A', 900);", txn_id=mvcc_txn)
        self.engine.commit_transaction(mvcc_txn)
        self.engine.execute("SET MODE 2PL;")
        self.assertIn(
            "transaction_mode=2PL_LOCKING",
            self.engine.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;"),
        )
        self.assertEqual(
            self.engine.execute("SELECT * FROM accounts WHERE id = 1;"),
            [{"id": 1, "owner": "A", "balance": 900}],
        )

    def test_deadlock_detection_still_works_after_mvcc_addition(self) -> None:
        txn_manager = self.engine.transaction_manager
        txn_manager.set_mode(TransactionMode.TWO_PL)
        tx1 = txn_manager.begin()
        tx2 = txn_manager.begin()
        step_one = threading.Event()
        step_two = threading.Event()
        results: dict[str, str] = {}

        def worker_one() -> None:
            txn_manager.before_write(tx1, "accounts:pk:1")
            step_one.set()
            step_two.wait(timeout=1)
            try:
                txn_manager.before_write(tx1, "accounts:pk:2")
                txn_manager.commit(tx1)
                results["tx1"] = "COMMITTED"
            except Exception:
                results["tx1"] = "ABORTED"

        def worker_two() -> None:
            step_one.wait(timeout=1)
            txn_manager.before_write(tx2, "accounts:pk:2")
            step_two.set()
            try:
                txn_manager.before_write(tx2, "accounts:pk:1")
                txn_manager.commit(tx2)
                results["tx2"] = "COMMITTED"
            except Exception:
                results["tx2"] = "ABORTED"

        thread_one = threading.Thread(target=worker_one)
        thread_two = threading.Thread(target=worker_two)
        thread_one.start()
        thread_two.start()
        thread_one.join(timeout=2)
        thread_two.join(timeout=2)
        self.assertEqual(results.get("tx2"), "ABORTED")
        self.assertIn("deadlock detected", "\n".join(txn_manager.lock_manager.logs))


if __name__ == "__main__":
    unittest.main()
