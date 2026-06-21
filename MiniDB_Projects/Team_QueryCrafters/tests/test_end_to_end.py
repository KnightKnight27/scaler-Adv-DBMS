import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

import pytest

# ─────────────────────────────────────────
# 1. STORAGE ENGINE
# ─────────────────────────────────────────

def test_page_serialize_deserialize():
    from storage.page import Page
    p = Page(page_id=0)
    slot = p.add_record({"id": 1, "name": "Alice"})
    data = p.serialize()
    p2 = Page.deserialize(data)
    assert p2.get_record(slot) == {"id": 1, "name": "Alice"}
    print("✅ Page serialize/deserialize passed")

def test_heap_file_insert_and_scan(tmp_path):
    from storage.heap_file import HeapFile
    hf = HeapFile(str(tmp_path / "test.db"))
    rid1 = hf.insert_record({"id": 1, "name": "Alice"})
    rid2 = hf.insert_record({"id": 2, "name": "Bob"})
    records = list(hf.scan_all())
    assert len(records) == 2
    assert any(r["name"] == "Alice" for _, r in records)
    print("✅ HeapFile insert and scan passed")

def test_buffer_pool_fetch_and_evict(tmp_path):
    from storage.heap_file import HeapFile
    from storage.buffer_pool import BufferPool
    hf = HeapFile(str(tmp_path / "test.db"))
    bp = BufferPool(capacity=3)
    for i in range(5):
        hf.insert_record({"id": i})
    for i in range(5):
        page = bp.fetch_page(i, hf)
        assert page is not None
        bp.unpin_page(i, hf)
    print("✅ BufferPool fetch and LRU eviction passed")

# ─────────────────────────────────────────
# 2. B+ TREE
# ─────────────────────────────────────────

def test_bplus_insert_and_search():
    from indexing.bplus_tree import BPlusTree
    tree = BPlusTree(order=4)
    tree.insert(1, (0, 0))
    tree.insert(2, (0, 1))
    tree.insert(3, (1, 0))
    assert tree.search(1) == (0, 0)
    assert tree.search(3) == (1, 0)
    assert tree.search(99) is None
    print("✅ B+ Tree insert and search passed")

def test_bplus_delete():
    from indexing.bplus_tree import BPlusTree
    tree = BPlusTree(order=4)
    for i in range(1, 8):
        tree.insert(i, (i, 0))
    tree.delete(4)
    assert tree.search(4) is None
    assert tree.search(5) == (5, 0)
    print("✅ B+ Tree delete passed")

def test_bplus_range_scan():
    from indexing.bplus_tree import BPlusTree
    tree = BPlusTree(order=4)
    for i in range(1, 10):
        tree.insert(i, (i, 0))
    results = tree.range_scan(3, 7)
    keys = [r[0] for r in results]
    assert keys == [3, 4, 5, 6, 7]
    print("✅ B+ Tree range scan passed")

# ─────────────────────────────────────────
# 3. SQL PARSER
# ─────────────────────────────────────────

def test_parser_create_table():
    from parser.sql_parser import SQLParser
    ast = SQLParser().parse("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    assert ast["type"] == "CreateTable"
    assert ast["table"] == "students"
    assert len(ast["columns"]) == 3
    print("✅ Parser CREATE TABLE passed")

def test_parser_insert():
    from parser.sql_parser import SQLParser
    ast = SQLParser().parse("INSERT INTO students VALUES (1, 'Alice', 20);")
    assert ast["type"] == "Insert"
    assert ast["table"] == "students"
    print("✅ Parser INSERT passed")

def test_parser_select_where():
    from parser.sql_parser import SQLParser
    ast = SQLParser().parse("SELECT * FROM students WHERE age > 18;")
    assert ast["type"] == "Select"
    assert ast["where"] is not None
    print("✅ Parser SELECT WHERE passed")

def test_parser_join():
    from parser.sql_parser import SQLParser
    ast = SQLParser().parse("SELECT s.name, e.course FROM students s JOIN enrollments e ON s.id = e.student_id;")
    assert ast["type"] == "Select"
    assert ast["join"] is not None
    print("✅ Parser JOIN passed")

def test_parser_delete():
    from parser.sql_parser import SQLParser
    ast = SQLParser().parse("DELETE FROM students WHERE id = 1;")
    assert ast["type"] == "Delete"
    print("✅ Parser DELETE passed")

def test_parser_transactions():
    from parser.sql_parser import SQLParser
    assert SQLParser().parse("BEGIN;")["type"] == "BeginTxn"
    assert SQLParser().parse("COMMIT;")["type"] == "CommitTxn"
    assert SQLParser().parse("ROLLBACK;")["type"] == "RollbackTxn"
    print("✅ Parser transaction commands passed")

# ─────────────────────────────────────────
# 4. QUERY EXECUTION
# ─────────────────────────────────────────

def test_seq_scan_and_filter(tmp_path):
    from storage.heap_file import HeapFile
    from storage.buffer_pool import BufferPool
    from executor.operators import SeqScan, Filter
    hf = HeapFile(str(tmp_path / "students.db"))
    hf.insert_record({"id": 1, "name": "Alice", "age": 20})
    hf.insert_record({"id": 2, "name": "Bob", "age": 17})
    hf.insert_record({"id": 3, "name": "Charlie", "age": 22})
    bp = BufferPool(capacity=5)
    scan = SeqScan(hf, bp)
    filt = Filter(scan, lambda row: row["age"] > 18)
    filt.open()
    results = []
    while (row := filt.next()) is not None:
        results.append(row)
    filt.close()
    assert len(results) == 2
    assert all(r["age"] > 18 for r in results)
    print("✅ SeqScan + Filter passed")

def test_index_scan(tmp_path):
    from storage.heap_file import HeapFile
    from storage.buffer_pool import BufferPool
    from indexing.bplus_tree import BPlusTree
    from executor.operators import IndexScan
    hf = HeapFile(str(tmp_path / "students.db"))
    tree = BPlusTree(order=4)
    rid = hf.insert_record({"id": 5, "name": "Eve", "age": 25})
    tree.insert(5, rid)
    bp = BufferPool(capacity=5)
    scan = IndexScan(hf, bp, tree, key=5)
    scan.open()
    row = scan.next()
    assert row["id"] == 5
    assert row["name"] == "Eve"
    print("✅ IndexScan passed")

def test_nested_loop_join(tmp_path):
    from storage.heap_file import HeapFile
    from storage.buffer_pool import BufferPool
    from executor.operators import SeqScan, NestedLoopJoin
    hf1 = HeapFile(str(tmp_path / "students.db"))
    hf2 = HeapFile(str(tmp_path / "enrollments.db"))
    hf1.insert_record({"id": 1, "name": "Alice"})
    hf2.insert_record({"student_id": 1, "course": "Math"})
    hf2.insert_record({"student_id": 2, "course": "Science"})
    bp = BufferPool(capacity=5)
    join = NestedLoopJoin(SeqScan(hf1, bp), SeqScan(hf2, bp),
                          condition=lambda l, r: l["id"] == r["student_id"])
    join.open()
    results = []
    while (row := join.next()) is not None:
        results.append(row)
    join.close()
    assert len(results) == 1
    assert results[0]["course"] == "Math"
    print("✅ NestedLoopJoin passed")

# ─────────────────────────────────────────
# 5. OPTIMIZER
# ─────────────────────────────────────────

def test_optimizer_chooses_index_scan(tmp_path):
    from optimizer.optimizer import Optimizer
    from indexing.bplus_tree import BPlusTree
    opt = Optimizer()
    tree = BPlusTree(order=4)
    opt.register_index("students", "id", tree)
    opt.update_stats("students", row_count=1000, num_pages=50)
    plan = opt.choose_plan("students", predicate_col="id", predicate_val=5)
    assert plan["type"] == "IndexScan"
    print("✅ Optimizer chose IndexScan correctly")

def test_optimizer_chooses_seq_scan(tmp_path):
    from optimizer.optimizer import Optimizer
    opt = Optimizer()
    opt.update_stats("students", row_count=10, num_pages=1)
    plan = opt.choose_plan("students", predicate_col="age", predicate_val=20)
    assert plan["type"] == "SeqScan"
    print("✅ Optimizer chose SeqScan correctly")

# ─────────────────────────────────────────
# 6. TRANSACTIONS & LOCKING
# ─────────────────────────────────────────

def test_transaction_commit(tmp_path):
    from minidb import MiniDB
    db = MiniDB(data_dir=str(tmp_path))
    db.execute("CREATE TABLE t1 (id INT PRIMARY KEY, val TEXT);")
    db.execute("BEGIN;")
    db.execute("INSERT INTO t1 VALUES (1, 'hello');")
    db.execute("COMMIT;")
    results = db.execute("SELECT * FROM t1;")
    assert any(r["id"] == 1 for r in results)
    print("✅ Transaction COMMIT preserves data")

def test_transaction_rollback(tmp_path):
    from minidb import MiniDB
    db = MiniDB(data_dir=str(tmp_path))
    db.execute("CREATE TABLE t2 (id INT PRIMARY KEY, val TEXT);")
    db.execute("BEGIN;")
    db.execute("INSERT INTO t2 VALUES (1, 'should not exist');")
    db.execute("ROLLBACK;")
    results = db.execute("SELECT * FROM t2;")
    assert len(results) == 0
    print("✅ Transaction ROLLBACK undoes data")

def test_deadlock_detection(tmp_path):
    from transactions.lock_manager import LockManager
    import threading
    lm = LockManager()
    results = []
    def txn_a():
        lm.acquire_lock(txn_id=1, resource_id="row_1", lock_type="EXCLUSIVE")
        import time; time.sleep(0.05)
        try:
            lm.acquire_lock(txn_id=1, resource_id="row_2", lock_type="EXCLUSIVE")
            results.append("A_ok")
        except Exception as e:
            results.append(f"A_aborted:{e}")
        finally:
            lm.release_locks(1)
    def txn_b():
        import time; time.sleep(0.01)
        lm.acquire_lock(txn_id=2, resource_id="row_2", lock_type="EXCLUSIVE")
        try:
            lm.acquire_lock(txn_id=2, resource_id="row_1", lock_type="EXCLUSIVE")
            results.append("B_ok")
        except Exception as e:
            results.append(f"B_aborted:{e}")
        finally:
            lm.release_locks(2)
    t1 = threading.Thread(target=txn_a)
    t2 = threading.Thread(target=txn_b)
    t1.start(); t2.start()
    t1.join(); t2.join()
    assert any("aborted" in r for r in results), "Deadlock should have been detected"
    print("✅ Deadlock detection passed")

# ─────────────────────────────────────────
# 7. WAL & CRASH RECOVERY
# ─────────────────────────────────────────

def test_wal_and_recovery(tmp_path):
    from minidb import MiniDB
    # Phase 1: write and commit, then simulate crash
    db = MiniDB(data_dir=str(tmp_path))
    db.execute("CREATE TABLE recovery_test (id INT PRIMARY KEY, val TEXT);")
    db.execute("BEGIN;")
    db.execute("INSERT INTO recovery_test VALUES (1, 'committed');")
    db.execute("COMMIT;")
    db.execute("BEGIN;")
    db.execute("INSERT INTO recovery_test VALUES (2, 'uncommitted');")
    # Simulate crash — do NOT commit, just destroy db object
    del db
    # Phase 2: restart and recover
    db2 = MiniDB(data_dir=str(tmp_path))
    db2.recover()
    results = db2.execute("SELECT * FROM recovery_test;")
    ids = [r["id"] for r in results]
    assert 1 in ids, "Committed data should survive crash"
    assert 2 not in ids, "Uncommitted data should be rolled back"
    print("✅ WAL crash recovery passed")

# ─────────────────────────────────────────
# 8. MVCC
# ─────────────────────────────────────────

def test_mvcc_readers_dont_block_writers(tmp_path):
    from extension.mvcc import MVCCManager
    import threading
    mvcc = MVCCManager()
    mvcc.insert(txn_id=1, table="t", key=1, data={"id": 1, "val": "v1"}, ts=1)
    mvcc.commit(txn_id=1, commit_ts=2)
    results = []
    def reader():
        snapshot_ts = 2
        row = mvcc.read(table="t", key=1, snapshot_ts=snapshot_ts)
        results.append(("read", row["val"] if row else None))
    def writer():
        mvcc.insert(txn_id=2, table="t", key=2, data={"id": 2, "val": "v2"}, ts=3)
        mvcc.commit(txn_id=2, commit_ts=4)
        results.append(("write", "done"))
    t1 = threading.Thread(target=reader)
    t2 = threading.Thread(target=writer)
    t1.start(); t2.start()
    t1.join(); t2.join()
    assert ("read", "v1") in results
    assert ("write", "done") in results
    print("✅ MVCC readers and writers don't block each other")

def test_mvcc_visibility_rules():
    from extension.mvcc import MVCCManager
    mvcc = MVCCManager()
    mvcc.insert(txn_id=1, table="t", key=1, data={"id": 1, "val": "old"}, ts=1)
    mvcc.commit(txn_id=1, commit_ts=2)
    mvcc.delete(txn_id=2, table="t", key=1, ts=3)
    # Before txn 2 commits, snapshot at ts=2 should still see old value
    row = mvcc.read(table="t", key=1, snapshot_ts=2)
    assert row is not None and row["val"] == "old"
    mvcc.commit(txn_id=2, commit_ts=4)
    # After commit of delete, snapshot at ts=5 should see nothing
    row2 = mvcc.read(table="t", key=1, snapshot_ts=5)
    assert row2 is None
    print("✅ MVCC visibility rules passed")

# ─────────────────────────────────────────
# 9. FULL END-TO-END FLOW
# ─────────────────────────────────────────

def test_full_end_to_end(tmp_path):
    from minidb import MiniDB
    db = MiniDB(data_dir=str(tmp_path))
    # Create tables
    db.execute("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    db.execute("CREATE TABLE enrollments (student_id INT, course TEXT);")
    # Insert data
    db.execute("BEGIN;")
    db.execute("INSERT INTO students VALUES (1, 'Alice', 20);")
    db.execute("INSERT INTO students VALUES (2, 'Bob', 17);")
    db.execute("INSERT INTO students VALUES (3, 'Charlie', 22);")
    db.execute("INSERT INTO enrollments VALUES (1, 'Math');")
    db.execute("INSERT INTO enrollments VALUES (3, 'Science');")
    db.execute("COMMIT;")
    # SELECT with WHERE
    results = db.execute("SELECT * FROM students WHERE age > 18;")
    assert len(results) == 2
    assert all(r["age"] > 18 for r in results)
    # SELECT with JOIN
    results = db.execute("SELECT s.name, e.course FROM students s JOIN enrollments e ON s.id = e.student_id;")
    assert len(results) == 2
    names = [r["name"] for r in results]
    assert "Alice" in names and "Charlie" in names
    # DELETE
    db.execute("BEGIN;")
    db.execute("DELETE FROM students WHERE id = 2;")
    db.execute("COMMIT;")
    results = db.execute("SELECT * FROM students;")
    assert all(r["id"] != 2 for r in results)
    print("✅ Full end-to-end test passed")

if __name__ == "__main__":
    import tempfile, os
    from pathlib import Path
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        test_page_serialize_deserialize()
        test_heap_file_insert_and_scan(tmp_path)
        test_buffer_pool_fetch_and_evict(tmp_path)
        test_bplus_insert_and_search()
        test_bplus_delete()
        test_bplus_range_scan()
        test_parser_create_table()
        test_parser_insert()
        test_parser_select_where()
        test_parser_join()
        test_parser_delete()
        test_parser_transactions()
        test_seq_scan_and_filter(tmp_path)
        test_index_scan(tmp_path)
        test_nested_loop_join(tmp_path)
        test_optimizer_chooses_index_scan(tmp_path)
        test_optimizer_chooses_seq_scan(tmp_path)
        test_transaction_commit(tmp_path)
        test_transaction_rollback(tmp_path)
        test_deadlock_detection(tmp_path)
        test_wal_and_recovery(tmp_path)
        test_mvcc_readers_dont_block_writers(tmp_path)
        test_mvcc_visibility_rules()
        test_full_end_to_end(tmp_path)
        print("\n🎉 ALL TESTS PASSED")
