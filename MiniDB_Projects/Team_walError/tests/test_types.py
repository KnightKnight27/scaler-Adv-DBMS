"""Tests for the type system and row codec (build step 2)."""

import pytest

from minidb.types import Column, ColumnType, Schema

INT = ColumnType.INT
TEXT = ColumnType.TEXT
FLOAT = ColumnType.FLOAT
BOOL = ColumnType.BOOL


def make_schema():
    return Schema(
        [
            Column("id", INT, nullable=False, primary_key=True),
            Column("name", TEXT),
            Column("score", FLOAT),
            Column("active", BOOL),
        ]
    )


def test_schema_basics():
    s = make_schema()
    assert len(s) == 4
    assert s.names == ["id", "name", "score", "active"]
    assert s.index_of("score") == 2
    assert s.primary_key_index() == 0
    assert s.column("name").type is TEXT


def test_schema_rejects_duplicates_and_empty():
    with pytest.raises(ValueError):
        Schema([])
    with pytest.raises(ValueError):
        Schema([Column("a", INT), Column("a", TEXT)])


@pytest.mark.parametrize(
    "row",
    [
        (1, "ada", 9.5, True),
        (2, "grace", -3.25, False),
        (3, None, None, None),          # nullable columns as NULL
        (4, "", 0.0, True),             # empty string, zero float
        (5, "héllo🌍", 1e10, False),     # unicode round-trip
    ],
)
def test_row_roundtrip(row):
    s = make_schema()
    assert s.decode(s.encode(row)) == row


def test_not_null_violation():
    s = make_schema()
    with pytest.raises(ValueError, match="NOT NULL"):
        s.encode((None, "x", 1.0, True))  # id is NOT NULL


def test_wrong_arity():
    s = make_schema()
    with pytest.raises(ValueError):
        s.encode((1, "x"))


def test_int_type_checks_reject_bool_and_float():
    s = Schema([Column("n", INT, nullable=False)])
    with pytest.raises(TypeError):
        s.encode((True,))   # bool is not a valid INT
    with pytest.raises(TypeError):
        s.encode((1.5,))    # float is not a valid INT


def test_null_bitmap_saves_space():
    s = make_schema()
    full = s.encode((1, "name", 1.0, True))
    mostly_null = s.encode((1, None, None, None))
    assert len(mostly_null) < len(full)  # NULLs occupy zero value bytes
