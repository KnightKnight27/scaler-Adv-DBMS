"""
Tests for Transaction Management and MVCC.
"""

import os
import sys
import unittest
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.transaction.lock_manager import LockManager, LockMode, DeadlockError
from src.transaction.transaction_manager import TransactionManager, TransactionState
from src.transaction.mvcc import MVCCManager
from src.storage.heap_file import RID


class TestLockManager(unittest.TestCase):
    def setUp(self):
        self.lm = LockManager(deadlock_timeout=0.5)

    def test_shared_locks(self):
        res = ('table1', RID(1, 0))
        self.assertTrue(self.lm.acquire(1, res, LockMode.SHARED))
        self.assertTrue(self.lm.acquire(2, res, LockMode.SHARED))
        holders = self.lm._lock_table[res].get_holders()
        self.assertEqual(holders, {1, 2})

    def test_exclusive_lock(self):
        res = ('table1', RID(1, 0))
        self.assertTrue(self.lm.acquire(1, res, LockMode.EXCLUSIVE))
        # Threaded test for blocking would be better, but we can test deadlock timeout
        with self.assertRaises(Exception):
            self.lm.acquire(2, res, LockMode.SHARED, timeout=0.1)

    def test_lock_upgrade(self):
        res = ('table1', RID(1, 0))
        self.assertTrue(self.lm.acquire(1, res, LockMode.SHARED))
        self.assertTrue(self.lm.acquire(1, res, LockMode.EXCLUSIVE))
        self.assertEqual(self.lm._lock_table[res].granted[0].mode, LockMode.EXCLUSIVE)

    def test_deadlock_detection(self):
        res1 = ('t', RID(1, 1))
        res2 = ('t', RID(1, 2))
        
        self.lm.acquire(1, res1, LockMode.EXCLUSIVE)
        self.lm.acquire(2, res2, LockMode.EXCLUSIVE)
        
        # We need threads to simulate deadlock as acquire blocks
        def txn1():
            try:
                self.lm.acquire(1, res2, LockMode.EXCLUSIVE, timeout=1.0)
            except DeadlockError:
                self.lm.release_all(1)
                
        def txn2():
            try:
                # Wait a bit so txn1 blocks first
                time.sleep(0.1)
                self.lm.acquire(2, res1, LockMode.EXCLUSIVE, timeout=1.0)
            except DeadlockError:
                self.lm.release_all(2)

        t1 = threading.Thread(target=txn1)
        t2 = threading.Thread(target=txn2)
        
        t1.start()
        t2.start()
        
        t1.join()
        t2.join()
        
        # Deadlock should have been resolved by aborting one


class TestMVCC(unittest.TestCase):
    def setUp(self):
        self.mvcc = MVCCManager()

    def test_snapshot_isolation(self):
        # Txn 1 starts and inserts
        self.mvcc.begin_transaction(1, 1)
        self.mvcc.insert_version('emp', 1, [1, 'Alice'], 1)
        self.mvcc.commit_transaction(1)
        
        # Txn 2 starts (takes snapshot at ts 2)
        self.mvcc.begin_transaction(2, 2)
        
        # Txn 3 starts after Txn 2 (takes snapshot at ts 3)
        self.mvcc.begin_transaction(3, 3)
        self.mvcc.insert_version('emp', 2, [2, 'Bob'], 3)
        self.mvcc.commit_transaction(3)
        
        # Txn 2 should see Alice (committed before Txn 2 started)
        self.assertEqual(self.mvcc.read_version('emp', 1, 2, 2), [1, 'Alice'])
        
        # Txn 2 should NOT see Bob (Txn 3 started after Txn 2's snapshot)
        self.assertIsNone(self.mvcc.read_version('emp', 2, 2, 2))
        
        # Txn 4 starts later and sees everything
        self.mvcc.begin_transaction(4, 4)
        self.assertEqual(self.mvcc.read_version('emp', 2, 4, 4), [2, 'Bob'])

    def test_update_visibility(self):
        self.mvcc.begin_transaction(1, 1)
        self.mvcc.insert_version('emp', 1, [1, 'Alice'], 1)
        self.mvcc.commit_transaction(1)
        
        self.mvcc.begin_transaction(2, 2)
        
        self.mvcc.begin_transaction(3, 3)
        self.mvcc.update_version('emp', 1, [1, 'Alice Updated'], 3)
        self.mvcc.commit_transaction(3)
        
        # Txn 2 still sees old version
        self.assertEqual(self.mvcc.read_version('emp', 1, 2, 2), [1, 'Alice'])
        
        # Txn 4 sees new version
        self.mvcc.begin_transaction(4, 4)
        self.assertEqual(self.mvcc.read_version('emp', 1, 4, 4), [1, 'Alice Updated'])


if __name__ == '__main__':
    unittest.main()
