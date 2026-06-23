"""Tests for the SQL tokenizer + parser (build step 8)."""

import pytest

from minidb.sql import (
    And,
    Begin,
    ColumnRef,
    Commit,
    Comparison,
    CreateIndex,
    CreateTable,
    Delete,
    Insert,
    Literal,
    Or,
    ParseError,
    Rollback,
    Select,
    parse,
    tokenize,
)
from minidb.types import ColumnType


# --- tokenizer ---------------------------------------------------------------


def kinds(sql):
    return [t.kind for t in tokenize(sql)]


def test_tokenize_basic_kinds():
    toks = tokenize("SELECT id FROM t WHERE x <= 3.5")
    assert [t.kind for t in toks[:-1]] == [
        "kw", "ident", "kw", "ident", "kw", "ident", "op", "float"
    ]


def test_tokenize_string_with_escaped_quote():
    toks = tokenize("'O''Brien'")
    assert toks[0].kind == "str" and toks[0].value == "O'Brien"


def test_tokenize_negative_number_in_values():
    toks = tokenize("VALUES (-5)")
    assert toks[2].kind == "int" and toks[2].value == -5


def test_tokenize_rejects_unterminated_string():
    with pytest.raises(ParseError):
        tokenize("'abc")


# --- CREATE TABLE ------------------------------------------------------------


def test_parse_create_table():
    stmt = parse(
        "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT)"
    )
    assert isinstance(stmt, CreateTable)
    assert stmt.table == "users"
    assert [c.name for c in stmt.columns] == ["id", "name", "age"]
    assert stmt.columns[0].primary_key and not stmt.columns[0].nullable
    assert stmt.columns[0].type is ColumnType.INT
    assert stmt.columns[1].type is ColumnType.TEXT and not stmt.columns[1].nullable
    assert stmt.columns[2].nullable


def test_parse_create_index():
    stmt = parse("CREATE INDEX ON users (age)")
    assert isinstance(stmt, CreateIndex)
    assert stmt.table == "users" and stmt.column == "age"


def test_create_table_unknown_type():
    with pytest.raises(ParseError):
        parse("CREATE TABLE t (x BLOB)")


# --- INSERT ------------------------------------------------------------------


def test_parse_insert_with_columns_multirow():
    stmt = parse("INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'y')")
    assert isinstance(stmt, Insert)
    assert stmt.table == "t"
    assert stmt.columns == ["a", "b"]
    assert stmt.rows == [[1, "x"], [2, "y"]]


def test_parse_insert_without_columns_and_literals():
    stmt = parse("INSERT INTO t VALUES (1, 2.5, 'hi', TRUE, NULL)")
    assert stmt.columns is None
    assert stmt.rows == [[1, 2.5, "hi", True, None]]


# --- SELECT ------------------------------------------------------------------


def test_parse_select_star():
    stmt = parse("SELECT * FROM t")
    assert isinstance(stmt, Select)
    assert stmt.star is True
    assert stmt.from_table == "t"
    assert stmt.where is None


def test_parse_select_columns_and_where():
    stmt = parse("SELECT id, name FROM users WHERE age >= 18")
    assert stmt.star is False
    assert [it.expr.name for it in stmt.items] == ["id", "name"]
    assert isinstance(stmt.where, Comparison)
    assert stmt.where.op == ">="
    assert isinstance(stmt.where.left, ColumnRef) and stmt.where.left.name == "age"
    assert isinstance(stmt.where.right, Literal) and stmt.where.right.value == 18


def test_where_precedence_or_of_ands():
    # a = 1 AND b = 2 OR c = 3  ==>  (a=1 AND b=2) OR (c=3)
    stmt = parse("SELECT * FROM t WHERE a = 1 AND b = 2 OR c = 3")
    assert isinstance(stmt.where, Or)
    assert isinstance(stmt.where.left, And)
    assert isinstance(stmt.where.right, Comparison)


def test_where_parentheses_override_precedence():
    stmt = parse("SELECT * FROM t WHERE a = 1 AND (b = 2 OR c = 3)")
    assert isinstance(stmt.where, And)
    assert isinstance(stmt.where.right, Or)


def test_parse_join_with_qualified_columns_and_aliases():
    stmt = parse(
        "SELECT u.name, o.total FROM users u "
        "JOIN orders o ON u.id = o.user_id WHERE o.total > 100"
    )
    assert stmt.from_table == "users" and stmt.from_alias == "u"
    assert len(stmt.joins) == 1
    j = stmt.joins[0]
    assert j.table == "orders" and j.alias == "o"
    assert isinstance(j.on, Comparison)
    assert j.on.left.table == "u" and j.on.left.name == "id"
    assert j.on.right.table == "o" and j.on.right.name == "user_id"
    assert stmt.where.left.table == "o"


# --- DELETE ------------------------------------------------------------------


def test_parse_delete_with_where():
    stmt = parse("DELETE FROM t WHERE id = 5")
    assert isinstance(stmt, Delete)
    assert stmt.table == "t"
    assert stmt.where.op == "="


def test_parse_delete_all():
    stmt = parse("DELETE FROM t")
    assert isinstance(stmt, Delete) and stmt.where is None


# --- transactions ------------------------------------------------------------


@pytest.mark.parametrize(
    "sql,node",
    [("BEGIN", Begin), ("BEGIN TRANSACTION", Begin), ("COMMIT", Commit),
     ("ROLLBACK", Rollback)],
)
def test_parse_txn_statements(sql, node):
    assert isinstance(parse(sql), node)


# --- errors ------------------------------------------------------------------


def test_trailing_input_rejected():
    with pytest.raises(ParseError):
        parse("SELECT * FROM t garbage extra")


def test_empty_statement_rejected():
    with pytest.raises(ParseError):
        parse("   ")


def test_trailing_semicolon_ok():
    assert isinstance(parse("SELECT * FROM t;"), Select)
