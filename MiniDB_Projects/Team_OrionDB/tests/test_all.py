import os
import sys
# Add parent directory to sys.path to enable local imports
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import shutil
import unittest
import threading
import time
from src.storage.disk_manager import DiskManager
from src.storage.page import Page
from src.storage.buffer_pool import BufferPoolManager
from src.storage.schema import Schema
from src.index.b_plus_tree import BPlusTree
from src.query.parser import SQLParser
from src.concurrency.lock_manager import LockManager
from src.concurrency.tx_manager import TransactionManager
from src.concurrency.mvcc import is_visible
from src.database import MiniDB

class TestMiniDB(unittest.TestCase):
    def setUp(self):
        self.test_dir = "./test_run_data"
        if not os.path.exists(self.test_dir):
            os.makedirs(self.test_dir)

    def tearDown(self):
        # We try to clean up but don't fail the test if Windows holds lock
        try:
            shutil.rmtree(self.test_dir)
        except OSError:
            pass

    # 1. Storage Layer Tests
    def test_disk_manager(self):
        db_path = os.path.join(self.test_dir, "test_dm.db")
        if os.path.exists(db_path):
            os.remove(db_path)
            
        dm = DiskManager(db_path)
        pid = dm.allocate_page()
        self.assertEqual(pid, 0)
        
        data = bytearray(4096)
        data[0] = 42
        dm.write_page(pid, data)
        
        read_data = dm.read_page(pid)
        self.assertEqual(read_data[0], 42)
        dm.close()

    def test_slotted_page(self):
        p = Page(page_id=5)
        self.assertEqual(p.get_page_id(), 5)
        self.assertEqual(p.get_num_slots(), 0)
        
        # Insert a record
        payload = b"hello world"
        slot_idx = p.insert_record(xmin=10, xmax=0, payload_bytes=payload)
        self.assertEqual(slot_idx, 0)
        self.assertEqual(p.get_num_slots(), 1)
        
        # Retrieve
        xmin, xmax, read_payload = p.get_record(0)
        self.assertEqual(xmin, 10)
        self.assertEqual(xmax, 0)
        self.assertEqual(read_payload, payload)
        
        # Soft delete
        p.delete_record(0, xmax=20)
        xmin, xmax, _ = p.get_record(0)
        self.assertEqual(xmax, 20)

    def test_buffer_pool(self):
        db_path = os.path.join(self.test_dir, "test_bp.db")
        if os.path.exists(db_path):
            os.remove(db_path)
            
        dm = DiskManager(db_path)
        bpm = BufferPoolManager(dm, pool_size=2)
        
        p0 = bpm.new_page()
        p1 = bpm.new_page()
        self.assertEqual(len(bpm.pool), 2)
        
        # Pin counts are 1
        self.assertEqual(bpm.pin_count[p0.get_page_id()], 1)
        self.assertEqual(bpm.pin_count[p1.get_page_id()], 1)
        
        # Try to allocate a 3rd page - should fail because all pages are pinned
        with self.assertRaises(RuntimeError):
            bpm.new_page()
            
        # Unpin p0
        bpm.unpin_page(p0.get_page_id(), is_dirty=True)
        # Allocate p2 - should evict p0
        p2 = bpm.new_page()
        self.assertNotIn(p0.get_page_id(), bpm.pool)
        self.assertIn(p2.get_page_id(), bpm.pool)
        
        dm.close()

    # 2. Indexing Layer Tests
    def test_b_plus_tree(self):
        db_path = os.path.join(self.test_dir, "test_tree.db")
        if os.path.exists(db_path):
            os.remove(db_path)
            
        dm = DiskManager(db_path)
        bpm = BufferPoolManager(dm, pool_size=10)
        
        tree = BPlusTree(bpm)
        # Insert elements to trigger splits
        tree.insert(10, (1, 0))
        tree.insert(20, (1, 1))
        tree.insert(30, (2, 0))
        tree.insert(40, (2, 1))
        tree.insert(50, (3, 0))
        
        # Verify search
        self.assertEqual(tree.search(30), (2, 0))
        self.assertEqual(tree.search(50), (3, 0))
        self.assertIsNone(tree.search(15))
        
        # Range scan
        res = tree.get_range(20, 40)
        self.assertEqual(len(res), 3)
        self.assertEqual(res[0][0], 20)
        self.assertEqual(res[2][0], 40)
        
        dm.close()

    # 3. SQL Parser Tests
    def test_sql_parser(self):
        sql = "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50))"
        p = SQLParser.parse(sql)
        self.assertEqual(p["type"], "CREATE")
        self.assertEqual(p["table_name"], "users")
        self.assertEqual(p["primary_key"], "id")
        self.assertIn("name", p["columns"])
        
        sql_insert = "INSERT INTO users VALUES (1, 'Alice')"
        p_ins = SQLParser.parse(sql_insert)
        self.assertEqual(p_ins["type"], "INSERT")
        self.assertEqual(p_ins["values"], [1, "Alice"])
        
        sql_select = "SELECT id, name FROM users JOIN orders ON users.id = orders.user_id WHERE users.id = 5"
        p_sel = SQLParser.parse(sql_select)
        self.assertEqual(p_sel["type"], "SELECT")
        self.assertEqual(p_sel["table_name"], "users")
        self.assertEqual(p_sel["join_table"], "orders")
        self.assertEqual(p_sel["join_condition"], ("users.id", "orders.user_id"))
        self.assertEqual(p_sel["where"], ("users.id", "=", 5))

    # 4. Locking & Deadlocks Tests
    def test_lock_manager_deadlock(self):
        lm = LockManager()
        
        self.assertTrue(lm.acquire_exclusive(1, "R1"))
        self.assertTrue(lm.acquire_exclusive(2, "R2"))
        
        lm.wait_for_graph[1].add(2)
        self.assertFalse(lm._has_deadlock())
        
        lm.wait_for_graph[2].add(1)
        self.assertTrue(lm._has_deadlock())

    # 5. MVCC Visibility Tests
    def test_mvcc_visibility(self):
        self.assertTrue(is_visible(xmin=10, xmax=0, tx_id=15, active_tx_ids={12}, committed_txs={10}))
        self.assertFalse(is_visible(xmin=12, xmax=0, tx_id=15, active_tx_ids={12}, committed_txs={10}))
        self.assertFalse(is_visible(xmin=5, xmax=10, tx_id=15, active_tx_ids={12}, committed_txs={10, 5}))
        self.assertTrue(is_visible(xmin=5, xmax=12, tx_id=15, active_tx_ids={12}, committed_txs={5}))

    # 6. Database End-To-End Integration Tests
    def test_db_end_to_end(self):
        db_folder = os.path.join(self.test_dir, "test_e2e_db")
        if os.path.exists(db_folder):
            try:
                shutil.rmtree(db_folder)
            except OSError:
                pass
                
        db = MiniDB(db_folder, is_mvcc=False)
        db.execute_sql("CREATE TABLE products (pid INT PRIMARY KEY, name VARCHAR(20), price INT)")
        
        db.execute_sql("INSERT INTO products VALUES (1, 'apple', 10)")
        db.execute_sql("INSERT INTO products VALUES (2, 'banana', 5)")
        
        # Run query
        res = db.execute_sql("SELECT name, price FROM products WHERE price = 10")
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0]["name"], "apple")
        self.assertEqual(res[0]["price"], 10)
        
        # Test Delete
        db.execute_sql("DELETE FROM products WHERE pid = 1")
        res2 = db.execute_sql("SELECT name FROM products")
        self.assertEqual(len(res2), 1)
        self.assertEqual(res2[0]["name"], "banana")
        
        db.close()

    # 7. WAL & Crash Recovery Test (ARIES-style)
    def test_crash_recovery(self):
        db_folder = os.path.join(self.test_dir, "test_recovery_db")
        if os.path.exists(db_folder):
            try:
                shutil.rmtree(db_folder)
            except OSError:
                pass

        db = MiniDB(db_folder, is_mvcc=False)
        db.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50))")

        # Committed transaction
        tx1 = db.tx_manager.begin()
        db.execute_sql("INSERT INTO users VALUES (1, 'Alice')", tx1)
        db.tx_manager.commit(tx1.tx_id)

        # Uncommitted transaction (loser)
        tx2 = db.tx_manager.begin()
        db.execute_sql("INSERT INTO users VALUES (2, 'Bob')", tx2)
        # Do NOT commit tx2

        # Simulate crash: close files without flushing buffer pool
        db.disk_manager.close()
        db.wal_manager.close()

        # Reopen and recover
        db2 = MiniDB(db_folder, is_mvcc=False)
        db2.recover()

        rows = db2.execute_sql("SELECT * FROM users")
        ids = sorted(r["id"] for r in rows)
        self.assertIn(1, ids, "Committed record (Alice) must survive crash recovery")
        self.assertNotIn(2, ids, "Uncommitted record (Bob) must be rolled back")

        db2.close()

if __name__ == "__main__":
    unittest.main()
