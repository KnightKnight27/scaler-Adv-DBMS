"""End-to-end SQL tests through the Database engine."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import pytest

from minidb.engine import Database
from minidb.sql.executor import IntegrityError


def _db(tmp_path):
    return Database(str(tmp_path / "shop"), pool_size=64)


def test_create_insert_select(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
    c.execute("INSERT INTO users VALUES (1,'alice',30),(2,'bob',25),(3,'carol',40)")
    r = c.execute("SELECT id, name FROM users WHERE age > 28")
    assert r.columns == ["id", "name"]
    assert sorted(r.rows) == [[1, "alice"], [3, "carol"]]
    db.close()


def test_primary_key_uniqueness(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    c.execute("INSERT INTO t VALUES (1,'a')")
    with pytest.raises(IntegrityError):
        c.execute("INSERT INTO t VALUES (1,'dup')")
    # the failed insert left nothing behind
    assert c.execute("SELECT id FROM t").rowcount == 1
    db.close()


def test_index_scan_used_when_cheaper(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    c.execute("INSERT INTO t VALUES " + ",".join(f"({i},{i*2})" for i in range(200)))
    plan_eq = c.execute("EXPLAIN SELECT v FROM t WHERE id = 42").message
    assert "IndexScan" in plan_eq            # PK equality -> index
    plan_scan = c.execute("EXPLAIN SELECT v FROM t WHERE v = 42").message
    assert "SeqScan" in plan_scan            # no index on v -> seq scan
    # correctness regardless of path
    assert c.execute("SELECT v FROM t WHERE id = 42").rows == [[84]]
    db.close()


def test_join(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT)")
    c.execute("CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, total INT)")
    c.execute("INSERT INTO users VALUES (1,'alice'),(2,'bob')")
    c.execute("INSERT INTO orders VALUES (10,1,100),(11,1,250),(12,2,75)")
    r = c.execute("SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid "
                  "WHERE o.total > 80")
    assert sorted(r.rows) == [["alice", 100], ["alice", 250]]
    db.close()


def test_delete_with_index(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    c.execute("INSERT INTO t VALUES " + ",".join(f"({i},{i})" for i in range(50)))
    assert c.execute("DELETE FROM t WHERE id = 25").rowcount == 1
    assert c.execute("SELECT id FROM t WHERE id = 25").rowcount == 0
    assert c.execute("SELECT id FROM t").rowcount == 49
    db.close()


def test_explicit_transaction_rollback(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    c.execute("INSERT INTO t VALUES (1,'keep')")
    c.execute("BEGIN")
    c.execute("INSERT INTO t VALUES (2,'discard')")
    assert c.execute("SELECT id FROM t").rowcount == 2
    c.execute("ROLLBACK")
    assert c.execute("SELECT id FROM t").rowcount == 1
    assert c.execute("SELECT id FROM t WHERE id = 1").rows == [[1]]
    db.close()


def test_durability_across_reopen(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    c.execute("INSERT INTO t VALUES (1,'alice'),(2,'bob')")
    db.close()                                # clean checkpoint + flush

    db2 = Database(str(tmp_path / "shop"), pool_size=64)
    c2 = db2.connect()
    assert c2.execute("SELECT v FROM t WHERE id = 2").rows == [["bob"]]
    db2.close()


def test_crash_recovery_through_engine(tmp_path):
    db = _db(tmp_path)
    c = db.connect()
    c.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    c.execute("INSERT INTO t VALUES (1,'committed')")
    # Uncommitted work, log forced but pages not checkpointed, then "crash".
    c.execute("BEGIN")
    c.execute("INSERT INTO t VALUES (2,'lost')")
    db.log.flush()
    db.log.close()                            # simulate crash: no checkpoint

    db2 = Database(str(tmp_path / "shop"), pool_size=64)
    c2 = db2.connect()
    rows = sorted(r[0] for r in c2.execute("SELECT id FROM t").rows)
    assert rows == [1]                        # committed kept, uncommitted undone
    assert db2.recovery_report.redo_count >= 1
    db2.close()
