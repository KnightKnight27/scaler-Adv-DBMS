"""
Tests for Storage Layer — DiskManager, SlottedPage, HeapFile, BufferPool.
"""

import os
import sys
import shutil
import unittest
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.storage.disk_manager import DiskManager
from src.storage.page import SlottedPage, serialize_record, deserialize_record
from src.storage.heap_file import HeapFile, RID
from src.storage.buffer_pool import BufferPool


class TestDiskManager(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.dm = DiskManager(self.test_dir)

    def tearDown(self):
        self.dm.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_create_file(self):
        self.dm.create_file('test.db')
        self.assertTrue(os.path.exists(os.path.join(self.test_dir, 'test.db')))

    def test_allocate_and_read_page(self):
        self.dm.create_file('test.db')
        page_id = self.dm.allocate_page('test.db')
        self.assertEqual(page_id, 1)  # Page 0 is header

        data = b'\x42' * self.dm.page_size
        self.dm.write_page('test.db', page_id, data)
        read_data = self.dm.read_page('test.db', page_id)
        self.assertEqual(read_data, data)

    def test_multiple_pages(self):
        self.dm.create_file('test.db')
        pages = []
        for i in range(5):
            pid = self.dm.allocate_page('test.db')
            pages.append(pid)

        self.assertEqual(len(pages), 5)
        self.assertEqual(self.dm.get_page_count('test.db'), 6)  # header + 5

    def test_deallocate_and_reuse(self):
        self.dm.create_file('test.db')
        p1 = self.dm.allocate_page('test.db')
        p2 = self.dm.allocate_page('test.db')
        self.dm.deallocate_page('test.db', p1)
        p3 = self.dm.allocate_page('test.db')
        self.assertEqual(p3, p1)  # Should reuse deallocated page


class TestSlottedPage(unittest.TestCase):
    def test_insert_and_get(self):
        page = SlottedPage(page_id=1)
        record = b'Hello, World!'
        slot = page.insert_record(record)
        self.assertIsNotNone(slot)
        self.assertEqual(page.get_record(slot), record)

    def test_multiple_records(self):
        page = SlottedPage(page_id=1)
        slots = []
        for i in range(10):
            record = f"Record {i}".encode()
            slot = page.insert_record(record)
            self.assertIsNotNone(slot)
            slots.append(slot)

        for i, slot in enumerate(slots):
            self.assertEqual(page.get_record(slot), f"Record {i}".encode())

    def test_delete_record(self):
        page = SlottedPage(page_id=1)
        slot = page.insert_record(b'Test data')
        self.assertTrue(page.delete_record(slot))
        self.assertIsNone(page.get_record(slot))

    def test_update_record(self):
        page = SlottedPage(page_id=1)
        slot = page.insert_record(b'Original')
        self.assertTrue(page.update_record(slot, b'Updated'))
        self.assertEqual(page.get_record(slot), b'Updated')

    def test_serialize_deserialize(self):
        page = SlottedPage(page_id=42)
        page.insert_record(b'Test')
        data = page.to_bytes()
        restored = SlottedPage.from_bytes(data)
        self.assertEqual(restored.page_id, 42)
        self.assertEqual(restored.get_record(0), b'Test')

    def test_get_all_records(self):
        page = SlottedPage(page_id=1)
        page.insert_record(b'A')
        page.insert_record(b'B')
        page.insert_record(b'C')
        page.delete_record(1)

        records = page.get_all_records()
        self.assertEqual(len(records), 2)  # A and C


class TestRecordSerialization(unittest.TestCase):
    def test_integer(self):
        types = ['INTEGER']
        data = serialize_record([42], types)
        result = deserialize_record(data, types)
        self.assertEqual(result, [42])

    def test_mixed_types(self):
        types = ['INTEGER', 'VARCHAR', 'FLOAT', 'BOOLEAN']
        values = [1, 'Alice', 50000.0, True]
        data = serialize_record(values, types)
        result = deserialize_record(data, types)
        self.assertEqual(result[0], 1)
        self.assertEqual(result[1], 'Alice')
        self.assertAlmostEqual(result[2], 50000.0, places=0)
        self.assertEqual(result[3], True)

    def test_null_values(self):
        types = ['INTEGER', 'VARCHAR']
        values = [None, 'Hello']
        data = serialize_record(values, types)
        result = deserialize_record(data, types)
        self.assertIsNone(result[0])
        self.assertEqual(result[1], 'Hello')


class TestHeapFile(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.dm = DiskManager(self.test_dir)
        self.col_types = ['INTEGER', 'VARCHAR', 'FLOAT']

    def tearDown(self):
        self.dm.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_insert_and_get(self):
        hf = HeapFile('test', self.dm, column_types=self.col_types)
        rid = hf.insert_record([1, 'Alice', 50000.0])
        result = hf.get_record(rid)
        self.assertEqual(result[0], 1)
        self.assertEqual(result[1], 'Alice')

    def test_scan(self):
        hf = HeapFile('test', self.dm, column_types=self.col_types)
        for i in range(5):
            hf.insert_record([i, f'Name{i}', float(i * 10000)])

        records = list(hf.scan())
        self.assertEqual(len(records), 5)

    def test_delete(self):
        hf = HeapFile('test', self.dm, column_types=self.col_types)
        rid = hf.insert_record([1, 'Alice', 50000.0])
        self.assertTrue(hf.delete_record(rid))
        self.assertIsNone(hf.get_record(rid))

    def test_record_count(self):
        hf = HeapFile('test', self.dm, column_types=self.col_types)
        for i in range(10):
            hf.insert_record([i, f'Name{i}', float(i)])
        self.assertEqual(hf.record_count(), 10)


class TestBufferPool(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.dm = DiskManager(self.test_dir)
        self.pool = BufferPool(self.dm, pool_size=5)

    def tearDown(self):
        self.dm.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_get_and_put(self):
        self.dm.create_file('test.db')
        pid = self.dm.allocate_page('test.db')
        page = self.pool.get_page('test.db', pid)
        self.assertIsNotNone(page)
        self.assertEqual(self.pool.miss_count, 1)

        # Second access should be cache hit
        page2 = self.pool.get_page('test.db', pid)
        self.assertEqual(self.pool.hit_count, 1)

    def test_eviction(self):
        self.dm.create_file('test.db')
        # Allocate more pages than pool size
        for i in range(7):
            pid = self.dm.allocate_page('test.db')
            page = self.pool.get_page('test.db', pid)
            self.pool.unpin('test.db', pid)

        stats = self.pool.get_stats()
        self.assertLessEqual(stats['pages_in_pool'], 5)

    def test_stats(self):
        stats = self.pool.get_stats()
        self.assertIn('hit_count', stats)
        self.assertIn('miss_count', stats)
        self.assertIn('hit_rate', stats)


if __name__ == '__main__':
    unittest.main()
