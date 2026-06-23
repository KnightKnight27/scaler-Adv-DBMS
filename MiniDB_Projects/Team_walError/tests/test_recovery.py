"""Tests for WAL + recovery (build step 12, core feature #6 — the crown jewel)."""

import pytest

from minidb import Database
from minidb.engine import MiniDBError


@pytest.fixture
def dbpath(tmp_path):
    return str(tmp_path / "recover.db")


def rows_of(db, sql):
    return sorted(db.execute(sql).rows)


# --- basic durability --------------------------------------------------------


def test_committed_autocommit_data_survives_reopen(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, name TEXT)")
    db.execute("INSERT INTO t VALUES (1,'ada'),(2,'grace')")
    db.close()

    db2 = Database(dbpath)  # reopen -> recovery replays committed WAL
    assert rows_of(db2, "SELECT id, name FROM t") == [(1, "ada"), (2, "grace")]
    db2.close()


def test_explicit_committed_txn_survives_crash(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    db.execute("BEGIN")
    db.execute("INSERT INTO t VALUES (1, 100)")
    db.execute("INSERT INTO t VALUES (2, 200)")
    db.execute("COMMIT")
    db.crash()  # no clean shutdown, no buffer flush

    db2 = Database(dbpath)
    assert rows_of(db2, "SELECT id, v FROM t") == [(1, 100), (2, 200)]
    db2.close()


# --- the crown jewel: uncommitted work vanishes ------------------------------


def test_uncommitted_txn_is_lost_after_crash(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT)")
    db.execute("INSERT INTO t VALUES (1, 1)")   # autocommit -> durable
    db.execute("BEGIN")
    db.execute("INSERT INTO t VALUES (2, 2)")   # uncommitted
    db.execute("INSERT INTO t VALUES (3, 3)")   # uncommitted
    # crash before COMMIT
    db.crash()

    db2 = Database(dbpath)
    # committed row 1 survives; uncommitted rows 2,3 are gone
    assert rows_of(db2, "SELECT id FROM t") == [(1,)]
    db2.close()


def test_mixed_committed_and_uncommitted(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY)")
    db.execute("BEGIN")
    db.execute("INSERT INTO t VALUES (10)")
    db.execute("COMMIT")                  # durable
    db.execute("BEGIN")
    db.execute("INSERT INTO t VALUES (20)")  # lost
    db.crash()

    db2 = Database(dbpath)
    assert rows_of(db2, "SELECT id FROM t") == [(10,)]
    db2.close()


def test_committed_delete_survives(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY)")
    db.execute("INSERT INTO t VALUES (1),(2),(3)")
    db.execute("DELETE FROM t WHERE id = 2")
    db.close()

    db2 = Database(dbpath)
    assert rows_of(db2, "SELECT id FROM t") == [(1,), (3,)]
    db2.close()


# --- ROLLBACK ----------------------------------------------------------------


def test_rollback_discards_changes(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY)")
    db.execute("INSERT INTO t VALUES (1)")
    db.execute("BEGIN")
    db.execute("INSERT INTO t VALUES (2)")
    db.execute("INSERT INTO t VALUES (3)")
    assert rows_of(db, "SELECT id FROM t") == [(1,), (2,), (3,)]  # visible in txn
    db.execute("ROLLBACK")
    assert rows_of(db, "SELECT id FROM t") == [(1,)]  # rolled back
    db.close()

    db2 = Database(dbpath)  # and rollback is durable across reopen
    assert rows_of(db2, "SELECT id FROM t") == [(1,)]
    db2.close()


# --- atomicity of a failed statement ----------------------------------------


def test_failed_insert_statement_is_atomic(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY)")
    db.execute("INSERT INTO t VALUES (1)")
    # second row duplicates PK -> whole statement must fail atomically
    with pytest.raises(MiniDBError, match="duplicate primary key"):
        db.execute("INSERT INTO t VALUES (2), (1)")
    assert rows_of(db, "SELECT id FROM t") == [(1,)]  # no partial (2) left behind
    db.close()


def test_recovery_stats_reported(dbpath):
    db = Database(dbpath)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY)")
    db.execute("INSERT INTO t VALUES (1),(2)")
    db.close()
    db2 = Database(dbpath)
    # create_table + 2 inserts = 3 applied operations from 1 committed txn-set
    assert db2.recovery_stats["applied"] >= 3
    db2.close()
