from __future__ import annotations

import sys
import threading
import time
import unittest
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))
TEST_RUNTIME_ROOT = ROOT / ".test_runtime"
TEST_RUNTIME_ROOT.mkdir(exist_ok=True)


def make_runtime_dir(name: str) -> Path:
    path = TEST_RUNTIME_ROOT / name
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True, exist_ok=True)
    return path

from minidb.buffer import BufferPoolManager
from minidb.engine import MiniDBEngine
from minidb.index import BPlusTree
from minidb.pages import PAGE_SIZE, Page, PageManager
from minidb.transactions import DeadlockError, LockManager, TransactionManager
from minidb.types import LockType, TransactionState


class PageAndBufferTests(unittest.TestCase):
    def test_page_allocation_read_write(self) -> None:
        runtime_dir = make_runtime_dir("page_allocation")
        page_manager = PageManager(runtime_dir / "pagefile.db")
        page_id = page_manager.allocate_page()
        page = page_manager.read_page(page_id)
        slot_id = page.insert(b"hello page")
        page_manager.write_page(page)
        reloaded = page_manager.read_page(page_id)
        self.assertEqual(reloaded.read(slot_id), b"hello page")
        self.assertEqual(page_manager.page_count, 1)

    def test_buffer_pool_hit_miss_eviction(self) -> None:
        runtime_dir = make_runtime_dir("buffer_pool")
        page_manager = PageManager(runtime_dir / "buffer.db")
        for _ in range(3):
            page_manager.allocate_page()
        buffer_pool = BufferPoolManager(page_manager, pool_size=2)
        buffer_pool.fetch_page(0)
        buffer_pool.unpin_page(0, is_dirty=False)
        buffer_pool.fetch_page(0)
        buffer_pool.unpin_page(0, is_dirty=False)
        buffer_pool.fetch_page(1)
        buffer_pool.unpin_page(1, is_dirty=False)
        buffer_pool.fetch_page(2)
        buffer_pool.unpin_page(2, is_dirty=False)
        logs = "\n".join(buffer_pool.debug_logs)
        self.assertIn("buffer miss page=0", logs)
        self.assertIn("buffer hit page=0", logs)
        self.assertIn("buffer eviction page=0", logs)


class StorageAndQueryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runtime_dir = make_runtime_dir(self._testMethodName)
        self.engine = MiniDBEngine(self.runtime_dir)
        self.engine.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);")
        self.engine.execute("CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, amount INT);")

    def tearDown(self) -> None:
        shutil.rmtree(self.runtime_dir, ignore_errors=True)

    def test_heap_insert_read_delete_scan(self) -> None:
        insert_one = self.engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        insert_two = self.engine.execute("INSERT INTO users VALUES (2, 'Bob', 28);")
        rid = insert_one["record_id"]
        self.assertEqual(self.engine.storage.read("users", rid)["name"], "Ada")
        deleted = self.engine.storage.delete("users", insert_two["record_id"])
        self.assertEqual(deleted["name"], "Bob")
        rows = self.engine.storage.scan("users")
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][1]["id"], 1)

    def test_bplus_tree_insert_search_delete(self) -> None:
        tree = BPlusTree(order=4)
        for key in range(1, 11):
            tree.insert(key, f"v{key}")
        self.assertEqual(tree.search(5), "v5")
        tree.delete(5)
        self.assertIsNone(tree.search(5))
        self.assertEqual(tree.search(6), "v6")

    def test_select_where_uses_index_scan(self) -> None:
        self.engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        self.engine.execute("INSERT INTO users VALUES (2, 'Bob', 28);")
        explain = self.engine.execute("EXPLAIN SELECT * FROM users WHERE id = 2;")
        rows = self.engine.execute("SELECT * FROM users WHERE id = 2;")
        self.assertIn("INDEX_SCAN", explain)
        self.assertEqual(rows, [{"id": 2, "name": "Bob", "age": 28}])

    def test_select_without_index_uses_table_scan(self) -> None:
        self.engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        self.engine.execute("INSERT INTO users VALUES (2, 'Bob', 28);")
        explain = self.engine.execute("EXPLAIN SELECT * FROM users WHERE age = 28;")
        rows = self.engine.execute("SELECT * FROM users WHERE age = 28;")
        self.assertIn("TABLE_SCAN", explain)
        self.assertEqual(rows, [{"id": 2, "name": "Bob", "age": 28}])

    def test_join_correctness(self) -> None:
        self.engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        self.engine.execute("INSERT INTO users VALUES (2, 'Bob', 28);")
        self.engine.execute("INSERT INTO orders VALUES (100, 1, 50);")
        self.engine.execute("INSERT INTO orders VALUES (101, 2, 70);")
        rows = self.engine.execute(
            "SELECT * FROM users JOIN orders ON users.id = orders.user_id;"
        )
        self.assertEqual(
            rows,
            [
                {
                    "users.id": 1,
                    "users.name": "Ada",
                    "users.age": 31,
                    "orders.id": 100,
                    "orders.user_id": 1,
                    "orders.amount": 50,
                },
                {
                    "users.id": 2,
                    "users.name": "Bob",
                    "users.age": 28,
                    "orders.id": 101,
                    "orders.user_id": 2,
                    "orders.amount": 70,
                },
            ],
        )

    def test_optimizer_join_order_reason(self) -> None:
        self.engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        self.engine.execute("INSERT INTO orders VALUES (100, 1, 50);")
        self.engine.execute("INSERT INTO orders VALUES (101, 1, 70);")
        explain = self.engine.execute(
            "EXPLAIN SELECT * FROM users JOIN orders ON users.id = orders.user_id;"
        )
        self.assertIn("NESTED_LOOP_JOIN", explain)
        self.assertIn("smaller input first", explain)

    def test_rollback_restores_deleted_rows(self) -> None:
        self.engine.execute("INSERT INTO users VALUES (1, 'Ada', 31);")
        self.engine.execute("BEGIN;")
        self.engine.execute("DELETE FROM users WHERE id = 1;")
        self.engine.execute("ROLLBACK;")
        rows = self.engine.execute("SELECT * FROM users;")
        self.assertEqual(rows, [{"id": 1, "name": "Ada", "age": 31}])

    def test_transaction_begin_commit_and_rollback(self) -> None:
        begin = self.engine.execute("BEGIN;")
        self.assertIn("txn_id", begin)
        commit = self.engine.execute("COMMIT;")
        self.assertEqual(commit["status"], "COMMITTED")
        begin = self.engine.execute("BEGIN;")
        self.assertIn("txn_id", begin)
        rollback = self.engine.execute("ROLLBACK;")
        self.assertEqual(rollback["status"], "ABORTED")


class LockingTests(unittest.TestCase):
    def test_shared_lock_compatibility(self) -> None:
        txn_manager = TransactionManager()
        txn1 = txn_manager.begin()
        txn2 = txn_manager.begin()
        txn_manager.before_read(txn1, "users:pk:1")
        txn_manager.before_read(txn2, "users:pk:1")
        holders = txn_manager.lock_manager.current_holders("users:pk:1")
        self.assertEqual(len(holders), 2)
        self.assertTrue(all(holder.lock_type == LockType.SHARED for holder in holders))

    def test_exclusive_lock_conflict(self) -> None:
        txn_manager = TransactionManager()
        txn1 = txn_manager.begin()
        txn2 = txn_manager.begin()
        txn_manager.before_write(txn1, "users:pk:1")
        entered_wait = threading.Event()
        finished = threading.Event()

        def acquire_write() -> None:
            entered_wait.set()
            txn_manager.before_write(txn2, "users:pk:1")
            finished.set()

        thread = threading.Thread(target=acquire_write)
        thread.start()
        entered_wait.wait(timeout=1)
        time.sleep(0.1)
        self.assertFalse(finished.is_set())
        txn_manager.commit(txn1)
        thread.join(timeout=1)
        self.assertTrue(finished.is_set())

    def test_locks_held_until_commit(self) -> None:
        txn_manager = TransactionManager()
        txn1 = txn_manager.begin()
        txn2 = txn_manager.begin()
        txn_manager.before_write(txn1, "users:pk:1")
        blocked = threading.Event()
        acquired = threading.Event()

        def acquire_read() -> None:
            blocked.set()
            txn_manager.before_read(txn2, "users:pk:1")
            acquired.set()

        thread = threading.Thread(target=acquire_read)
        thread.start()
        blocked.wait(timeout=1)
        time.sleep(0.1)
        self.assertFalse(acquired.is_set())
        txn_manager.commit(txn1)
        thread.join(timeout=1)
        self.assertTrue(acquired.is_set())

    def test_deadlock_detection_and_victim_abort(self) -> None:
        txn_manager = TransactionManager()
        txn1 = txn_manager.begin()
        txn2 = txn_manager.begin()
        step_one = threading.Event()
        step_two = threading.Event()
        results: dict[str, str] = {}

        def worker_one() -> None:
            txn_manager.before_write(txn1, "users:pk:1")
            step_one.set()
            step_two.wait(timeout=1)
            try:
                txn_manager.before_write(txn1, "users:pk:2")
                txn_manager.commit(txn1)
                results["txn1"] = "COMMITTED"
            except (DeadlockError, Exception):
                results["txn1"] = "ABORTED"

        def worker_two() -> None:
            step_one.wait(timeout=1)
            txn_manager.before_write(txn2, "users:pk:2")
            step_two.set()
            try:
                txn_manager.before_write(txn2, "users:pk:1")
                txn_manager.commit(txn2)
                results["txn2"] = "COMMITTED"
            except (DeadlockError, Exception):
                results["txn2"] = "ABORTED"

        thread_one = threading.Thread(target=worker_one)
        thread_two = threading.Thread(target=worker_two)
        thread_one.start()
        thread_two.start()
        thread_one.join(timeout=2)
        thread_two.join(timeout=2)
        self.assertEqual(results.get("txn2"), "ABORTED")
        self.assertIn("deadlock detected", "\n".join(txn_manager.lock_manager.logs))


if __name__ == "__main__":
    unittest.main()
