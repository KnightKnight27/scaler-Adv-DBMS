"""
Tests for end-to-end query execution.
"""

import os
import sys
import shutil
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from run import MiniDB


class TestQueryExecution(unittest.TestCase):
    """End-to-end tests using the MiniDB engine."""

    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.db = MiniDB(db_dir=self.test_dir, pool_size=50)

    def tearDown(self):
        self.db.buffer_pool.flush_all()
        self.db.disk_manager.close()
        self.db.wal.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_create_table(self):
        result = self.db.execute_sql(
            "CREATE TABLE test (id INTEGER PRIMARY KEY, name VARCHAR)"
        )
        self.assertIn("created", result.lower())

    def test_insert_and_select(self):
        self.db.execute_sql(
            "CREATE TABLE test (id INTEGER PRIMARY KEY, name VARCHAR, val FLOAT)"
        )
        self.db.execute_sql(
            "INSERT INTO test (id, name, val) VALUES (1, 'Alice', 100.0)"
        )
        result = self.db.execute_sql("SELECT * FROM test")
        self.assertIn('Alice', result)
        self.assertIn('1', result)

    def test_select_where(self):
        self.db.execute_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, x INTEGER)"
        )
        for i in range(5):
            self.db.execute_sql(f"INSERT INTO t (id, x) VALUES ({i}, {i * 10})")

        result = self.db.execute_sql("SELECT * FROM t WHERE x > 20")
        self.assertIn('30', result)
        self.assertIn('40', result)
        self.assertNotIn(' 10 ', result)

    def test_delete(self):
        self.db.execute_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, name VARCHAR)"
        )
        self.db.execute_sql("INSERT INTO t (id, name) VALUES (1, 'Alice')")
        self.db.execute_sql("INSERT INTO t (id, name) VALUES (2, 'Bob')")

        result = self.db.execute_sql("DELETE FROM t WHERE id = 1")
        self.assertIn("Deleted 1", result)

        result = self.db.execute_sql("SELECT * FROM t")
        self.assertIn('Bob', result)
        self.assertNotIn('Alice', result)

    def test_join(self):
        self.db.execute_sql(
            "CREATE TABLE emp (id INTEGER PRIMARY KEY, name VARCHAR, dept_id INTEGER)"
        )
        self.db.execute_sql(
            "CREATE TABLE dept (dept_id INTEGER PRIMARY KEY, dept_name VARCHAR)"
        )
        self.db.execute_sql("INSERT INTO emp (id, name, dept_id) VALUES (1, 'Alice', 10)")
        self.db.execute_sql("INSERT INTO dept (dept_id, dept_name) VALUES (10, 'Engineering')")

        result = self.db.execute_sql(
            "SELECT emp.name, dept.dept_name FROM emp "
            "JOIN dept ON emp.dept_id = dept.dept_id"
        )
        self.assertIn('Alice', result)
        self.assertIn('Engineering', result)

    def test_order_by(self):
        self.db.execute_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, val INTEGER)"
        )
        self.db.execute_sql("INSERT INTO t (id, val) VALUES (1, 30)")
        self.db.execute_sql("INSERT INTO t (id, val) VALUES (2, 10)")
        self.db.execute_sql("INSERT INTO t (id, val) VALUES (3, 20)")

        result = self.db.execute_sql("SELECT * FROM t ORDER BY val")
        lines = result.strip().split('\n')
        # Data rows are after header and separator
        data_lines = [l for l in lines if '|' in l][1:]  # Skip header
        # First data row should have val=10
        self.assertIn('10', data_lines[0])

    def test_limit(self):
        self.db.execute_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, val INTEGER)"
        )
        for i in range(10):
            self.db.execute_sql(f"INSERT INTO t (id, val) VALUES ({i}, {i})")

        result = self.db.execute_sql("SELECT * FROM t LIMIT 3")
        self.assertIn("3 row", result)

    def test_aggregate_count(self):
        self.db.execute_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, val INTEGER)"
        )
        for i in range(5):
            self.db.execute_sql(f"INSERT INTO t (id, val) VALUES ({i}, {i})")

        result = self.db.execute_sql("SELECT COUNT(*) FROM t")
        self.assertIn('5', result)

    def test_drop_table(self):
        self.db.execute_sql("CREATE TABLE t (id INTEGER PRIMARY KEY)")
        result = self.db.execute_sql("DROP TABLE t")
        self.assertIn("dropped", result.lower())

    def test_demo_data(self):
        result = self.db.run_demo()
        self.assertIn('10 employees', result)

        # Verify data
        result = self.db.execute_sql("SELECT * FROM employees")
        self.assertIn('Alice', result)

    def test_explain(self):
        self.db.execute_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, val INTEGER)"
        )
        plan = self.db.explain("SELECT * FROM t WHERE id = 1")
        self.assertIn('Scan', plan)


if __name__ == '__main__':
    unittest.main()
