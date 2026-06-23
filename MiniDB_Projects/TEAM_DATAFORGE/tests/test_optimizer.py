"""
Tests for Cost-Based Optimizer.
"""

import os
import sys
import tempfile
import shutil
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.catalog.catalog import Catalog, ColumnInfo
from src.optimizer.statistics import TableStatistics
from src.optimizer.cost_estimator import CostEstimator
from src.storage.disk_manager import DiskManager
from src.storage.buffer_pool import BufferPool
from src.storage.heap_file import HeapFile
from src.parser.parser import Parser


class TestOptimizer(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.catalog = Catalog(self.test_dir)
        self.dm = DiskManager(self.test_dir)
        self.bp = BufferPool(self.dm, 50)
        
        # Create a test table
        cols = [
            ColumnInfo('id', 'INTEGER', True, False),
            ColumnInfo('age', 'INTEGER', False, True),
            ColumnInfo('salary', 'FLOAT', False, True)
        ]
        self.catalog.create_table('emp', cols, 'id')
        self.catalog.create_index('idx_age', 'emp', 'age')
        
        self.hf = HeapFile('emp', self.dm, self.bp, ['INTEGER', 'INTEGER', 'FLOAT'])
        for i in range(100):
            self.hf.insert_record([i, 20 + (i % 40), 50000.0 + i * 100])
            
        self.stats = TableStatistics(self.catalog)
        self.stats.analyze_table('emp', self.hf)
        
        self.estimator = CostEstimator(self.catalog)

    def tearDown(self):
        self.dm.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_statistics_collection(self):
        table_info = self.catalog.get_table('emp')
        self.assertIsNotNone(table_info.stats)
        s = table_info.stats
        self.assertEqual(s.row_count, 100)
        self.assertEqual(s.min_values['id'], 0)
        self.assertEqual(s.max_values['id'], 99)
        self.assertEqual(s.distinct_values['age'], 40)

    def test_selectivity_equality(self):
        ast = Parser("SELECT * FROM emp WHERE age = 25").parse()
        sel = self.estimator.estimate_selectivity('emp', ast.where)
        # 1 / 40 distinct ages
        self.assertAlmostEqual(sel, 1.0 / 40.0)

    def test_selectivity_range(self):
        ast = Parser("SELECT * FROM emp WHERE id < 50").parse()
        sel = self.estimator.estimate_selectivity('emp', ast.where)
        # roughly 50%
        self.assertTrue(0.4 <= sel <= 0.6)

    def test_should_use_index(self):
        # Mock larger table size to make index scan cheaper
        table_info = self.catalog.get_table('emp')
        table_info.stats.row_count = 10000
        table_info.stats.page_count = 100
        
        # High selectivity (few rows) -> should use index
        self.assertTrue(self.estimator.should_use_index('emp', 'age', 0.01))
        # Low selectivity (many rows) -> sequential scan is better
        self.assertFalse(self.estimator.should_use_index('emp', 'age', 0.8))


if __name__ == '__main__':
    unittest.main()
