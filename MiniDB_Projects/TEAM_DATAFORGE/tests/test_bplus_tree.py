"""
Tests for B+ Tree Index.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.index.bplus_tree import BPlusTree
from src.storage.heap_file import RID


class TestBPlusTree(unittest.TestCase):
    def test_insert_and_search(self):
        tree = BPlusTree(order=4)
        tree.insert(10, RID(1, 0))
        tree.insert(20, RID(1, 1))
        tree.insert(30, RID(1, 2))

        self.assertEqual(tree.search(10), RID(1, 0))
        self.assertEqual(tree.search(20), RID(1, 1))
        self.assertEqual(tree.search(30), RID(1, 2))
        self.assertIsNone(tree.search(15))

    def test_insert_many_with_splits(self):
        tree = BPlusTree(order=4)
        for i in range(100):
            tree.insert(i, RID(1, i))

        self.assertEqual(len(tree), 100)
        for i in range(100):
            self.assertEqual(tree.search(i), RID(1, i))

    def test_delete(self):
        tree = BPlusTree(order=4)
        for i in range(10):
            tree.insert(i, RID(1, i))

        self.assertTrue(tree.delete(5))
        self.assertIsNone(tree.search(5))
        self.assertEqual(len(tree), 9)

    def test_delete_nonexistent(self):
        tree = BPlusTree(order=4)
        tree.insert(1, RID(1, 0))
        self.assertFalse(tree.delete(999))

    def test_range_search(self):
        tree = BPlusTree(order=4)
        for i in range(20):
            tree.insert(i, RID(1, i))

        results = tree.range_search(5, 15)
        keys = [k for k, _ in results]
        self.assertEqual(keys, list(range(5, 16)))

    def test_scan_all(self):
        tree = BPlusTree(order=4)
        for i in [5, 3, 8, 1, 9, 2, 7]:
            tree.insert(i, RID(1, i))

        results = tree.scan_all()
        keys = [k for k, _ in results]
        self.assertEqual(keys, [1, 2, 3, 5, 7, 8, 9])  # Sorted

    def test_min_max(self):
        tree = BPlusTree(order=4)
        for i in [50, 20, 80, 10, 90]:
            tree.insert(i, RID(1, i))

        self.assertEqual(tree.get_min_key(), 10)
        self.assertEqual(tree.get_max_key(), 90)

    def test_empty_tree(self):
        tree = BPlusTree(order=4)
        self.assertTrue(tree.is_empty())
        self.assertIsNone(tree.search(1))
        self.assertEqual(len(tree), 0)

    def test_contains(self):
        tree = BPlusTree(order=4)
        tree.insert(42, RID(1, 0))
        self.assertIn(42, tree)
        self.assertNotIn(99, tree)

    def test_large_insert_delete(self):
        tree = BPlusTree(order=5)
        n = 500
        for i in range(n):
            tree.insert(i, RID(1, i))
        self.assertEqual(len(tree), n)

        # Delete half
        for i in range(0, n, 2):
            tree.delete(i)

        self.assertEqual(len(tree), n // 2)
        for i in range(n):
            if i % 2 == 0:
                self.assertIsNone(tree.search(i))
            else:
                self.assertEqual(tree.search(i), RID(1, i))


if __name__ == '__main__':
    unittest.main()
