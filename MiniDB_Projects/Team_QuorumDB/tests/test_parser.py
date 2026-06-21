"""Tests for the SQL tokenizer and parser."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import pytest

from minidb.sql import ast
from minidb.sql.parser import ParseError, parse, Parser


def test_create_table_with_pk_and_not_null():
    s = parse("CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT)")
    assert isinstance(s, ast.CreateTable)
    assert s.name == "users"
    assert s.pk_column == "id"
    assert [c.name for c in s.columns] == ["id", "name", "age"]
    assert s.columns[1].not_null is True
    assert s.columns[0].type == "INT"


def test_create_index_unique():
    s = parse("CREATE UNIQUE INDEX ON users (name)")
    assert isinstance(s, ast.CreateIndex)
    assert (s.table, s.column, s.unique) == ("users", "name", True)


def test_insert_multiple_rows():
    s = parse("INSERT INTO users (id, name) VALUES (1, 'alice'), (2, 'bob')")
    assert isinstance(s, ast.Insert)
    assert s.columns == ["id", "name"]
    assert s.rows == [[1, "alice"], [2, "bob"]]


def test_insert_negative_and_float_and_bool():
    s = parse("INSERT INTO t VALUES (-5, 3.14, TRUE, NULL)")
    assert s.rows == [[-5, 3.14, True, None]]


def test_select_star_with_where_and():
    s = parse("SELECT * FROM users WHERE age >= 18 AND name = 'alice'")
    assert s.columns == ["*"]
    assert s.from_table == "users"
    assert len(s.where.comparisons) == 2
    c0 = s.where.comparisons[0]
    assert (c0.left.name, c0.op, c0.right.value) == ("age", ">=", 18)


def test_select_join():
    s = parse("SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid "
              "WHERE o.total > 100")
    assert s.from_table == "users" and s.from_alias == "u"
    assert s.columns == ["u.name", "o.total"]
    assert len(s.joins) == 1
    j = s.joins[0]
    assert j.table == "orders"
    assert j.on.is_join_predicate
    assert (j.on.left.table, j.on.left.name) == ("u", "id")


def test_delete_with_where():
    s = parse("DELETE FROM users WHERE id = 7")
    assert isinstance(s, ast.Delete)
    assert s.where.comparisons[0].right.value == 7


def test_txn_control():
    assert isinstance(parse("BEGIN"), ast.Begin)
    assert isinstance(parse("COMMIT"), ast.Commit)
    assert isinstance(parse("ROLLBACK"), ast.Rollback)


def test_script_split():
    stmts = Parser.parse_script("BEGIN; INSERT INTO t VALUES (1); COMMIT;")
    assert [type(s).__name__ for s in stmts] == ["Begin", "Insert", "Commit"]


def test_errors():
    with pytest.raises(ParseError):
        parse("SELECT FROM users")  # missing columns
    with pytest.raises(ParseError):
        parse("INSERT INTO t VALUES (1")  # unbalanced paren
