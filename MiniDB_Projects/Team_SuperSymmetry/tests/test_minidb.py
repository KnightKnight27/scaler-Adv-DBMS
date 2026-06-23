"""
MiniDB test suite.

Runs with pytest (`pytest tests/`) or standalone (`python tests/test_minidb.py`).
Covers: types/serialization, slotted pages, B+ tree, buffer pool + WAL rule,
SQL parsing, end-to-end SQL, 2PL transactions + abort, deadlock detection,
crash recovery (redo + undo), and MVCC snapshot isolation.
"""
import os
import random
import shutil
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb import Database, TransactionError
from minidb.btree import BPlusTree
from minidb.lock_manager import DeadlockError, LockManager, LockMode
from minidb.page import Page
from minidb.sql import Parser
from minidb.types import Column, DataType, Schema

TMP = "/tmp/minidb_tests"


def _dir(name):
    d = os.path.join(TMP, name)
    shutil.rmtree(d, ignore_errors=True)
    return d


# ---- unit: types & pages --------------------------------------------------
def test_schema_roundtrip():
    s = Schema([Column("id", DataType.INT), Column("name", DataType.TEXT),
                Column("score", DataType.FLOAT), Column("ok", DataType.BOOL)])
    for rec in ([1, "alice", 3.5, True], [2, None, None, False], [3, "x" * 100, -1.0, None]):
        assert s.deserialize(s.serialize(rec)) == rec


def test_page_insert_get_delete():
    p = Page()
    a = p.insert_record(b"hello")
    b = p.insert_record(b"world")
    assert p.get_record(a) == b"hello"
    assert p.get_record(b) == b"world"
    assert p.delete_record(a) is True
    assert p.get_record(a) is None
    assert p.get_record(b) == b"world"


# ---- unit: B+ tree --------------------------------------------------------
def test_btree_against_oracle():
    tree = BPlusTree(order=8)
    oracle = {}
    rng = random.Random(7)
    for _ in range(2000):
        k = rng.randrange(300)
        rid = (k, rng.randrange(5))
        tree.insert(k, rid)
        oracle.setdefault(k, [])
        if rid not in oracle[k]:
            oracle[k].append(rid)
    for k in range(300):
        assert set(tree.search(k)) == set(oracle.get(k, []))
    lo, hi = 50, 150
    got = set(tree.range_scan(lo, hi))
    exp = set(r for k, rs in oracle.items() if lo <= k <= hi for r in rs)
    assert got == exp


# ---- unit: lock manager ---------------------------------------------------
def test_lock_shared_compatible():
    lm = LockManager()
    lm.acquire(1, "r", LockMode.S)
    lm.acquire(2, "r", LockMode.S)  # must not block
    lm.release_all(1)
    lm.release_all(2)


def test_lock_deadlock_detected():
    lm = LockManager()
    lm.acquire(1, "a", LockMode.X)
    lm.acquire(2, "b", LockMode.X)
    barrier = threading.Barrier(2)
    errs = []

    def t1():
        barrier.wait()
        try:
            lm.acquire(1, "b", LockMode.X, timeout=2)
        except DeadlockError:
            errs.append(1)

    def t2():
        barrier.wait()
        try:
            lm.acquire(2, "a", LockMode.X, timeout=2)
        except DeadlockError:
            errs.append(2)

    th = [threading.Thread(target=t1), threading.Thread(target=t2)]
    [t.start() for t in th]
    [t.join() for t in th]
    assert len(errs) >= 1  # at least one victim


# ---- unit: SQL parser -----------------------------------------------------
def test_parser_select_join():
    q = Parser("SELECT a.x, b.y FROM a JOIN b ON a.k = b.k WHERE a.x > 3").parse()
    assert q.table == "a"
    assert len(q.joins) == 1
    assert q.where.conjuncts[0].op == ">"


# ---- integration: end-to-end SQL -----------------------------------------
def test_crud_join_aggregate():
    db = Database(_dir("crud"), isolation="2PL")
    db.execute("CREATE TABLE u (id INT PRIMARY KEY, name TEXT, age INT)")
    db.execute("CREATE TABLE o (oid INT PRIMARY KEY, uid INT, amt FLOAT)")
    db.execute("INSERT INTO u VALUES (1,'a',30),(2,'b',25),(3,'c',40)")
    db.execute("INSERT INTO o VALUES (10,1,9.5),(11,1,2.0),(12,2,5.0)")
    assert len(db.execute("SELECT * FROM u")) == 3
    assert len(db.execute("SELECT id FROM u WHERE age > 28")) == 2
    j = db.execute("SELECT u.name, o.amt FROM u JOIN o ON u.id = o.uid")
    assert len(j) == 3
    agg = {r["uid"]: r["COUNT(*)"] for r in
           db.execute("SELECT uid, COUNT(*) FROM o GROUP BY uid")}
    assert agg == {1: 2, 2: 1}
    db.execute("DELETE FROM u WHERE id = 2")
    assert len(db.execute("SELECT * FROM u")) == 2
    db.close()


def test_index_used_at_scale():
    db = Database(_dir("idx"), isolation="2PL")
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    db.execute("INSERT INTO t VALUES " + ",".join(f"({i},{i})" for i in range(500)))
    db.analyze()
    assert "IndexScan" in db.explain("SELECT v FROM t WHERE id = 250")
    assert db.execute("SELECT v FROM t WHERE id = 250").rows[0]["v"] == 250
    db.close()


# ---- integration: transactions -------------------------------------------
def test_abort_rolls_back():
    db = Database(_dir("abort"), isolation="2PL")
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, name TEXT)")
    db.execute("INSERT INTO t VALUES (1,'a'),(2,'b')")
    txn = db.begin()
    db.execute("INSERT INTO t VALUES (3,'c')", txn=txn)
    db.execute("DELETE FROM t WHERE id = 1", txn=txn)
    db.abort(txn)
    ids = sorted(r["id"] for r in db.execute("SELECT id FROM t"))
    assert ids == [1, 2]                       # insert undone, delete undone
    assert len(db.execute("SELECT id FROM t WHERE id = 3")) == 0
    db.close()


def test_deadlock_one_victim():
    db = Database(_dir("dl"), isolation="2PL")
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    db.execute("INSERT INTO t VALUES (1,1),(2,2)")
    barrier = threading.Barrier(2)
    out = {}

    def work(name, first, second):
        txn = db.begin()
        try:
            db.lock_mgr.acquire(txn.txn_id, ("t", first), LockMode.X)
            barrier.wait()
            time.sleep(0.05)
            db.lock_mgr.acquire(txn.txn_id, ("t", second), LockMode.X)
            db.commit(txn)
            out[name] = "ok"
        except DeadlockError:
            db.abort(txn)
            out[name] = "victim"

    th = [threading.Thread(target=work, args=("T1", (0, 0), (0, 1))),
          threading.Thread(target=work, args=("T2", (0, 1), (0, 0)))]
    [t.start() for t in th]
    [t.join() for t in th]
    assert "ok" in out.values() and "victim" in out.values()
    db.close()


# ---- integration: recovery -----------------------------------------------
def test_crash_recovery():
    d = _dir("recover")
    db = Database(d, isolation="2PL")
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    db.execute("INSERT INTO t VALUES (1,'keep'),(2,'keep')")
    bad = db.begin()
    db.execute("INSERT INTO t VALUES (3,'lose'),(4,'lose')", txn=bad)
    db.bufferpool.flush_all(); db.disk.fsync_all(); db.wal.flush()  # steal
    db.disk.close()
    del db, bad                                   # crash (no commit/close)

    db2 = Database(d, isolation="2PL")
    ids = sorted(r["id"] for r in db2.execute("SELECT id FROM t"))
    assert ids == [1, 2]                          # committed kept, losers undone
    assert db2.last_recovery["undo_applied"] >= 2
    db2.close()


# ---- integration: MVCC ----------------------------------------------------
def test_mvcc_snapshot_isolation():
    db = Database(_dir("mvcc"), isolation="MVCC")
    db.execute("CREATE TABLE acct (id INT PRIMARY KEY, bal INT)")
    db.execute("INSERT INTO acct VALUES (1,100)")
    t_reader = db.begin()                         # snapshot before the write
    snap = db.execute("SELECT bal FROM acct WHERE id = 1", txn=t_reader)
    assert snap.rows[0]["bal"] == 100

    w = db.begin()
    db.execute("DELETE FROM acct WHERE id = 1", txn=w)
    db.execute("INSERT INTO acct VALUES (1, 999)", txn=w)
    db.commit(w)

    # fresh transaction sees the new value
    t_new = db.begin()
    assert db.execute("SELECT bal FROM acct WHERE id = 1", txn=t_new).rows[0]["bal"] == 999
    db.commit(t_new)
    # old snapshot is unchanged
    assert db.execute("SELECT bal FROM acct WHERE id = 1", txn=t_reader).rows[0]["bal"] == 100
    db.commit(t_reader)
    db.close()


# ---- runner ---------------------------------------------------------------
def _run_all():
    tests = [v for k, v in sorted(globals().items())
             if k.startswith("test_") and callable(v)]
    passed = 0
    for t in tests:
        try:
            t()
            print(f"  PASS  {t.__name__}")
            passed += 1
        except Exception as e:
            print(f"  FAIL  {t.__name__}: {e}")
            raise
    print(f"\n{passed}/{len(tests)} tests passed")


if __name__ == "__main__":
    _run_all()
