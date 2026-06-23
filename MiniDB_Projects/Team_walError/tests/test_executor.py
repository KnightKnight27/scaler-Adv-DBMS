"""Tests for the Volcano executor + end-to-end SQL via the engine (build step 9)."""

import pytest

from minidb import Database
from minidb.engine import MiniDBError


@pytest.fixture
def db():
    d = Database(":memory:")
    yield d
    d.close()


def seed_users(db):
    db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
    db.execute(
        "INSERT INTO users VALUES (1,'ada',36),(2,'grace',40),(3,'linus',25),(4,'guido',55)"
    )


# --- DDL / DML ---------------------------------------------------------------


def test_create_and_insert(db):
    seed_users(db)
    r = db.execute("SELECT * FROM users")
    assert r.columns == ["id", "name", "age"]
    assert r.rowcount == 4


def test_duplicate_pk_rejected(db):
    seed_users(db)
    with pytest.raises(MiniDBError, match="duplicate primary key"):
        db.execute("INSERT INTO users VALUES (1,'dup',1)")


def test_insert_with_column_subset_fills_nulls(db):
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, a TEXT, b INT)")
    db.execute("INSERT INTO t (id, a) VALUES (1, 'x')")
    rows = db.execute("SELECT id, a, b FROM t").rows
    assert rows == [(1, "x", None)]


def test_insert_missing_not_null_rejected(db):
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, a TEXT NOT NULL)")
    with pytest.raises(MiniDBError):
        db.execute("INSERT INTO t (id) VALUES (1)")


# --- SELECT / WHERE / projection --------------------------------------------


def test_projection_columns_order(db):
    seed_users(db)
    r = db.execute("SELECT name, id FROM users WHERE id = 2")
    assert r.columns == ["name", "id"]
    assert r.rows == [("grace", 2)]


def test_where_comparisons(db):
    seed_users(db)
    older = db.execute("SELECT name FROM users WHERE age >= 40").rows
    assert sorted(n for (n,) in older) == ["grace", "guido"]


def test_where_and_or(db):
    seed_users(db)
    r = db.execute(
        "SELECT name FROM users WHERE age < 30 OR age > 50"
    ).rows
    assert sorted(n for (n,) in r) == ["guido", "linus"]
    r2 = db.execute(
        "SELECT name FROM users WHERE age > 30 AND age < 50"
    ).rows
    assert sorted(n for (n,) in r2) == ["ada", "grace"]


def test_select_alias(db):
    seed_users(db)
    r = db.execute("SELECT name AS who FROM users WHERE id = 1")
    assert r.columns == ["who"]
    assert r.rows == [("ada",)]


# --- DELETE ------------------------------------------------------------------


def test_delete_with_where(db):
    seed_users(db)
    r = db.execute("DELETE FROM users WHERE age < 30")
    assert r.rowcount == 1
    remaining = db.execute("SELECT id FROM users").rows
    assert sorted(i for (i,) in remaining) == [1, 2, 4]


def test_delete_all(db):
    seed_users(db)
    assert db.execute("DELETE FROM users").rowcount == 4
    assert db.execute("SELECT * FROM users").rowcount == 0


def test_delete_then_reinsert_same_pk(db):
    seed_users(db)
    db.execute("DELETE FROM users WHERE id = 1")
    db.execute("INSERT INTO users VALUES (1, 'reborn', 1)")  # PK freed by delete
    assert db.execute("SELECT name FROM users WHERE id = 1").rows == [("reborn",)]


# --- JOIN --------------------------------------------------------------------


def test_inner_join(db):
    db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT)")
    db.execute("CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, total INT)")
    db.execute("INSERT INTO users VALUES (1,'ada'),(2,'grace')")
    db.execute("INSERT INTO orders VALUES (10,1,100),(11,1,250),(12,2,50)")
    r = db.execute(
        "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid "
        "WHERE o.total > 60"
    )
    assert r.columns == ["name", "total"]
    assert sorted(r.rows) == [("ada", 100), ("ada", 250)]


def test_join_three_way(db):
    db.execute("CREATE TABLE a (id INT PRIMARY KEY, v INT)")
    db.execute("CREATE TABLE b (id INT PRIMARY KEY, av INT, w INT)")
    db.execute("CREATE TABLE c (id INT PRIMARY KEY, bw INT, label TEXT)")
    db.execute("INSERT INTO a VALUES (1, 10)")
    db.execute("INSERT INTO b VALUES (2, 10, 20)")
    db.execute("INSERT INTO c VALUES (3, 20, 'match')")
    r = db.execute(
        "SELECT c.label FROM a JOIN b ON a.v = b.av JOIN c ON b.w = c.bw"
    )
    assert r.rows == [("match",)]


# --- error handling ----------------------------------------------------------


def test_unknown_column_errors(db):
    seed_users(db)
    with pytest.raises(MiniDBError, match="unknown column"):
        db.execute("SELECT nope FROM users")


def test_explain_renders_plan_tree(db):
    from minidb import sql as ast
    from minidb.executor import build_naive_plan

    seed_users(db)
    plan = build_naive_plan(
        ast.parse("SELECT name FROM users WHERE age > 30"), db.catalog
    )
    text = plan.explain()
    assert "Project" in text and "Filter" in text and "SeqScan" in text
