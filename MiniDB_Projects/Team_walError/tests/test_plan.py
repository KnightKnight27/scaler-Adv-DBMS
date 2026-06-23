"""Tests for the cost-based optimizer (build step 10)."""

import pytest

from minidb import Database
from minidb import sql as ast
from minidb.executor import (
    Filter,
    IndexScan,
    NestedLoopJoin,
    Project,
    SeqScan,
    build_naive_plan,
    materialize,
)
from minidb.plan import conjuncts, optimize


@pytest.fixture
def db():
    d = Database(":memory:")
    d.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
    rows = ",".join(f"({i},'u{i}',{20 + i % 40})" for i in range(200))
    d.execute(f"INSERT INTO users VALUES {rows}")
    yield d
    d.close()


def child_chain(op):
    """Walk down child/outer to collect operator types."""
    seen = []
    while op is not None:
        seen.append(type(op).__name__)
        op = getattr(op, "child", None) or getattr(op, "outer", None)
    return seen


def test_conjuncts_splits_on_and():
    where = ast.parse("SELECT * FROM t WHERE a=1 AND b=2 AND c=3").where
    assert len(conjuncts(where)) == 3


def test_pk_equality_uses_index_scan(db):
    plan = optimize(ast.parse("SELECT name FROM users WHERE id = 42"), db.catalog)
    types = child_chain(plan)
    assert "IndexScan" in types
    assert "SeqScan" not in types


def test_non_indexed_predicate_uses_seq_scan(db):
    plan = optimize(ast.parse("SELECT name FROM users WHERE age = 30"), db.catalog)
    types = child_chain(plan)
    assert "SeqScan" in types
    assert "IndexScan" not in types


def test_secondary_index_is_used_when_present(db):
    db.execute("CREATE INDEX ON users (age)")
    db.catalog.get_table("users").analyze()
    plan = optimize(ast.parse("SELECT name FROM users WHERE age = 30"), db.catalog)
    assert "IndexScan" in child_chain(plan)


def test_index_scan_returns_correct_rows(db):
    r = db.execute("SELECT name FROM users WHERE id = 42")
    assert r.rows == [("u42",)]


def test_range_predicate_on_pk_uses_index(db):
    plan = optimize(
        ast.parse("SELECT id FROM users WHERE id >= 10 AND id <= 12"), db.catalog
    )
    assert "IndexScan" in child_chain(plan)
    r = db.execute("SELECT id FROM users WHERE id >= 10 AND id <= 12")
    assert sorted(i for (i,) in r.rows) == [10, 11, 12]


def test_join_order_puts_smaller_relation_outer(db):
    # tiny "tags" table joined to big "users": optimizer should scan tags as outer
    db.execute("CREATE TABLE tags (uid INT PRIMARY KEY, tag TEXT)")
    db.execute("INSERT INTO tags VALUES (1,'vip'),(2,'vip')")
    plan = optimize(
        ast.parse("SELECT u.name, t.tag FROM users u JOIN tags t ON u.id = t.uid"),
        db.catalog,
    )
    # find the join; its outer side should ultimately scan 'tags'
    assert isinstance(plan, Project)
    join = plan.child
    assert isinstance(join, NestedLoopJoin)
    outer_types = child_chain(join.outer)
    assert any("Scan" in t for t in outer_types)
    # outer should be the small table (tags)
    leaf = join.outer
    while getattr(leaf, "child", None) or getattr(leaf, "outer", None):
        leaf = getattr(leaf, "child", None) or getattr(leaf, "outer", None)
    assert leaf.table.name == "tags"


@pytest.mark.parametrize(
    "query",
    [
        "SELECT * FROM users WHERE id = 7",
        "SELECT name FROM users WHERE age = 30",
        "SELECT id, name FROM users WHERE id >= 5 AND id <= 9",
        "SELECT name FROM users WHERE age > 50 OR id = 1",
    ],
)
def test_optimized_results_match_naive(db, query):
    """The optimizer must never change answers vs the naive SeqScan plan."""
    select = ast.parse(query)
    optimized = sorted(materialize(optimize(select, db.catalog)).rows)
    naive = sorted(materialize(build_naive_plan(select, db.catalog)).rows)
    assert optimized == naive


def test_explain_output(db):
    r = db.execute("EXPLAIN SELECT name FROM users WHERE id = 42")
    text = "\n".join(row[0] for row in r.rows)
    assert "IndexScan" in text
    assert "est_rows" in text
    assert "Project" in text
