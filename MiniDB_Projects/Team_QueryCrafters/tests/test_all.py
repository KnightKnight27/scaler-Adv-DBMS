import os
import time
import json
import pytest
import threading
from src.storage.page import Page
from src.storage.heap_file import HeapFile
from src.storage.buffer_pool import BufferPool
from src.indexing.bplus_tree import BPlusTree
from src.parser.sql_parser import SQLParser
from src.minidb import MiniDB
from src.transactions.lock_manager import TransactionAbortException

# 1. Page serialization/deserialization round-trip
def test_page_serialization():
    p = Page(page_id=42, flags=1)
    slot1 = p.add_record(b"first_record")
    slot2 = p.add_record(b"second_record")
    assert slot1 == 0
    assert slot2 == 1
    
    serialized = p.serialize()
    assert len(serialized) == Page.PAGE_SIZE
    
    p2 = Page(page_id=0)
    p2.deserialize(serialized)
    assert p2.page_id == 42
    assert p2.flags == 1
    assert p2.get_record(0) == b"first_record"
    assert p2.get_record(1) == b"second_record"
    
    p2.delete_record(0)
    assert p2.get_record(0) is None
    assert p2.get_record(1) == b"second_record"

# 2. Buffer pool LRU eviction
def test_buffer_pool_lru(tmp_path):
    # Setup mock heap file
    heap_file = HeapFile("mock_table", str(tmp_path))
    # Create empty pages on disk
    for i in range(5):
        heap_file.write_page_to_disk(i, Page(i).serialize())
        
    bp = BufferPool(capacity=3)
    p0 = bp.fetch_page(0, heap_file)
    p1 = bp.fetch_page(1, heap_file)
    p2 = bp.fetch_page(2, heap_file)
    
    assert len(bp.pool) == 3
    
    # Try to fetch another page when all are pinned
    with pytest.raises(RuntimeError):
        bp.fetch_page(3, heap_file)
        
    # Unpin pages
    bp.unpin_page(0, heap_file)
    bp.unpin_page(1, heap_file)
    
    # Evict page 0 (least recently used among unpinned)
    p3 = bp.fetch_page(3, heap_file)
    assert len(bp.pool) == 3
    assert (heap_file.table_name, 0) not in bp.pool
    assert (heap_file.table_name, 3) in bp.pool

# 3. B+ tree insert/search/delete/range scan
def test_bplus_tree(tmp_path):
    idx_path = os.path.join(tmp_path, "test.idx")
    tree = BPlusTree(idx_path)
    
    # Insert multiple keys to trigger splits
    keys = [10, 20, 5, 15, 30, 25, 35]
    for k in keys:
        tree.insert(k, (1, k))
        
    # Search
    for k in keys:
        assert tree.search(k) == (1, k)
        
    # Range scan
    scan = tree.range_scan(10, 25)
    assert sorted(scan) == [(1, 10), (1, 15), (1, 20), (1, 25)]
    
    # Delete
    tree.delete(15)
    assert tree.search(15) is None
    assert sorted(tree.range_scan(10, 25)) == [(1, 10), (1, 20), (1, 25)]
    
    # Persist and reload
    tree.save()
    tree2 = BPlusTree(idx_path)
    assert tree2.search(10) == (1, 10)
    assert tree2.search(15) is None

# 4. Parser correctness for all SQL types
def test_sql_parser():
    p1 = SQLParser("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    ast1 = p1.parse()
    assert ast1["type"] == "CreateTable"
    assert ast1["table_name"] == "students"
    assert ast1["columns"][0]["name"] == "id"
    assert ast1["columns"][0]["primary_key"] is True
    
    p2 = SQLParser("INSERT INTO students VALUES (1, 'Alice', 20);")
    ast2 = p2.parse()
    assert ast2["type"] == "Insert"
    assert ast2["table_name"] == "students"
    assert ast2["values"] == [1, "Alice", 20]
    
    p3 = SQLParser("SELECT * FROM students;")
    ast3 = p3.parse()
    assert ast3["type"] == "Select"
    assert ast3["columns"] == ["*"]
    
    p4 = SQLParser("SELECT id, name FROM students WHERE age > 18;")
    ast4 = p4.parse()
    assert ast4["type"] == "Select"
    assert ast4["columns"] == ["id", "name"]
    assert ast4["where"]["left"] == "age"
    assert ast4["where"]["op"] == ">"
    assert ast4["where"]["right"] == 18
    
    p5 = SQLParser("SELECT s.name, e.course FROM students s JOIN enrollments e ON s.id = e.student_id;")
    ast5 = p5.parse()
    assert ast5["type"] == "Select"
    assert ast5["from_table"] == "students"
    assert ast5["from_alias"] == "s"
    assert ast5["join"]["table"] == "enrollments"
    assert ast5["join"]["alias"] == "e"
    assert ast5["join"]["on_left"] == "s.id"
    assert ast5["join"]["on_right"] == "e.student_id"
    
    p6 = SQLParser("DELETE FROM students WHERE id = 1;")
    ast6 = p6.parse()
    assert ast6["type"] == "Delete"
    assert ast6["table_name"] == "students"
    assert ast6["where"]["left"] == "id"
    assert ast6["where"]["right"] == 1
    
    assert SQLParser("BEGIN;").parse()["type"] == "BeginTxn"
    assert SQLParser("COMMIT;").parse()["type"] == "CommitTxn"
    assert SQLParser("ROLLBACK;").parse()["type"] == "RollbackTxn"

# 5-8. SeqScan, IndexScan, Filter, Projection, JOIN execution
def test_query_execution(tmp_path):
    db = MiniDB(data_dir=str(tmp_path), verbose=False, txn_mode="2PL")
    db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    db.execute_sql("CREATE TABLE enrollments (student_id INT PRIMARY KEY, course TEXT);")
    
    db.execute_sql("INSERT INTO students VALUES (1, 'Alice', 20);")
    db.execute_sql("INSERT INTO students VALUES (2, 'Bob', 17);")
    db.execute_sql("INSERT INTO enrollments VALUES (1, 'DBMS');")
    
    # 5. SeqScan returns all rows
    res = db.execute_sql("SELECT * FROM students;")
    assert len(res) == 2
    
    # 6. IndexScan returns correct row
    res = db.execute_sql("SELECT name FROM students WHERE id = 1;")
    assert len(res) == 1
    assert res[0]["name"] == "Alice"
    
    # 7. WHERE filter correctness
    res = db.execute_sql("SELECT * FROM students WHERE age > 18;")
    assert len(res) == 1
    assert res[0]["name"] == "Alice"
    
    # 8. JOIN correctness
    res = db.execute_sql("SELECT s.name, e.course FROM students s JOIN enrollments e ON s.id = e.student_id;")
    assert len(res) == 1
    assert res[0]["s.name"] == "Alice"
    assert res[0]["e.course"] == "DBMS"

# 9-10. Transaction commit/rollback
def test_transaction_commit_and_rollback(tmp_path):
    db = MiniDB(data_dir=str(tmp_path), verbose=False, txn_mode="2PL")
    db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    
    # Transaction commit preserves data
    db.execute_sql("BEGIN;")
    db.execute_sql("INSERT INTO students VALUES (1, 'Alice', 20);")
    db.execute_sql("COMMIT;")
    res = db.execute_sql("SELECT * FROM students;")
    assert len(res) == 1
    
    # Transaction rollback undoes data
    db.execute_sql("BEGIN;")
    db.execute_sql("INSERT INTO students VALUES (2, 'Bob', 22);")
    db.execute_sql("ROLLBACK;")
    res = db.execute_sql("SELECT * FROM students;")
    assert len(res) == 1
    assert res[0]["name"] == "Alice"

# 11. Deadlock detection triggers abort
def test_deadlock_detection(tmp_path):
    db = MiniDB(data_dir=str(tmp_path), verbose=False, txn_mode="2PL")
    db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    db.execute_sql("INSERT INTO students VALUES (1, 'Alice', 20);")
    db.execute_sql("INSERT INTO students VALUES (2, 'Bob', 22);")
    
    # We will run two threads acquiring locks in reverse order
    # Txn 1: Lock student 1 EXCLUSIVE, then lock student 2 EXCLUSIVE
    # Txn 2: Lock student 2 EXCLUSIVE, then lock student 1 EXCLUSIVE
    txn1_id = db.transaction_manager.begin_transaction("2PL")
    txn2_id = db.transaction_manager.begin_transaction("2PL")
    
    # Hold lock 1 in txn 1
    db.lock_manager.acquire_lock(txn1_id, "row:students:1", "EXCLUSIVE")
    # Hold lock 2 in txn 2
    db.lock_manager.acquire_lock(txn2_id, "row:students:2", "EXCLUSIVE")
    
    # Create thread to simulate txn 1 waiting for student 2
    def run_txn1():
        db.lock_manager.acquire_lock(txn1_id, "row:students:2", "EXCLUSIVE")
        
    t1 = threading.Thread(target=run_txn1)
    t1.start()
    
    time.sleep(0.1) # Wait for t1 to block
    
    # Txn 2 requests student 1: deadlock cycle!
    # Txn 2 is the youngest (txn2_id > txn1_id), so it must be aborted
    with pytest.raises(TransactionAbortException):
        db.lock_manager.acquire_lock(txn2_id, "row:students:1", "EXCLUSIVE")
        
    # Cleanup locks
    db.transaction_manager.rollback(txn1_id)
    db.transaction_manager.rollback(txn2_id)
    t1.join()

# 12. WAL recovery restores committed data
def test_wal_recovery(tmp_path):
    db = MiniDB(data_dir=str(tmp_path), verbose=False, txn_mode="2PL")
    db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    
    # Write some committed records
    db.execute_sql("BEGIN;")
    db.execute_sql("INSERT INTO students VALUES (1, 'Alice', 20);")
    db.execute_sql("COMMIT;")
    
    # Write an uncommitted record (active at crash)
    db.execute_sql("BEGIN;")
    db.execute_sql("INSERT INTO students VALUES (2, 'Bob', 22);")
    
    # Close WAL
    db.wal_manager.close()
    
    # Delete DB file
    db_file_path = os.path.join(tmp_path, "students.db")
    if os.path.exists(db_file_path):
        os.remove(db_file_path)
        
    # Re-instantiate DB: recovery should analysis, redo all, and undo Bob
    db2 = MiniDB(data_dir=str(tmp_path), verbose=False, txn_mode="2PL")
    res = db2.execute_sql("SELECT * FROM students;")
    assert len(res) == 1
    assert res[0]["name"] == "Alice"

# 13. MVCC visibility rules
def test_mvcc_visibility(tmp_path):
    db = MiniDB(data_dir=str(tmp_path), verbose=False, txn_mode="MVCC")
    db.execute_sql("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);")
    db.execute_sql("INSERT INTO students VALUES (1, 'Alice', 20);")
    
    # Start Txn 1 (reader)
    txn1 = db.transaction_manager.begin_transaction("MVCC")
    
    # Start Txn 2 (writer)
    txn2 = db.transaction_manager.begin_transaction("MVCC")
    db.executor.execute(SQLParser("INSERT INTO students VALUES (2, 'Bob', 22);").parse(), txn2)
    
    # Reader Txn 1 should NOT see Bob since Txn 2 is uncommitted
    res_txn1 = db.executor.execute(SQLParser("SELECT * FROM students;").parse(), txn1)
    assert len(res_txn1) == 1
    assert res_txn1[0]["name"] == "Alice"
    
    # Commit Txn 2
    db.transaction_manager.commit(txn2)
    
    # Reader Txn 1 should STILL NOT see Bob (snapshot isolation)
    res_txn1_again = db.executor.execute(SQLParser("SELECT * FROM students;").parse(), txn1)
    assert len(res_txn1_again) == 1
    
    # New Transaction (Txn 3) should see Bob
    txn3 = db.transaction_manager.begin_transaction("MVCC")
    res_txn3 = db.executor.execute(SQLParser("SELECT * FROM students;").parse(), txn3)
    assert len(res_txn3) == 2
    
    # Cleanup
    db.transaction_manager.commit(txn1)
    db.transaction_manager.commit(txn3)
