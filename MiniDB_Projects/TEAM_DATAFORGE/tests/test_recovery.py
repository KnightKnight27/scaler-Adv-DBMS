"""
Tests for Write-Ahead Log and Recovery Manager.
"""

import os
import sys
import tempfile
import shutil
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.recovery.wal import WAL, LogRecord, LogRecordType
from src.recovery.recovery_manager import RecoveryManager


class TestWAL(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.wal = WAL(self.test_dir)

    def tearDown(self):
        self.wal.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_append_and_read(self):
        rec = LogRecord(txn_id=1, record_type=LogRecordType.INSERT, data={'val': 42})
        lsn = self.wal.append(rec)
        self.wal.flush()
        
        records = self.wal.read_all()
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].lsn, lsn)
        self.assertEqual(records[0].data['val'], 42)

    def test_multiple_records(self):
        for i in range(10):
            self.wal.append(LogRecord(txn_id=1, record_type=LogRecordType.BEGIN))
        self.wal.flush()
        
        records = self.wal.read_all()
        self.assertEqual(len(records), 10)


class TestRecoveryManager(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.wal = WAL(self.test_dir)
        self.rm = RecoveryManager(self.wal)

    def tearDown(self):
        self.wal.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_recovery_phases(self):
        # Txn 1 commits
        self.rm.log_begin(1)
        self.rm.log_insert(1, 't', [1, 'A'])
        self.rm.log_commit(1)
        
        # Txn 2 aborts
        self.rm.log_begin(2)
        self.rm.log_insert(2, 't', [2, 'B'])
        self.rm.log_abort(2)
        
        # Txn 3 is active at crash
        self.rm.log_begin(3)
        self.rm.log_insert(3, 't', [3, 'C'])
        
        self.rm.flush()
        
        committed, active = self.rm.recover()
        self.assertEqual(committed, {1})
        self.assertEqual(active, {3})
        
        data = self.rm.get_committed_data()
        self.assertIn('t', data)
        self.assertEqual(data['t']['inserts'][0], [1, 'A'])
        
        # B and C should not be in committed data
        for val in data['t']['inserts']:
            self.assertNotEqual(val, [2, 'B'])
            self.assertNotEqual(val, [3, 'C'])


if __name__ == '__main__':
    unittest.main()
