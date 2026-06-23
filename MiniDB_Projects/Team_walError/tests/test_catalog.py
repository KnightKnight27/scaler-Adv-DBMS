"""Tests for the system catalog (build step 7)."""

import pytest

from minidb.btree import DuplicateKeyError
from minidb.buffer_pool import BufferPool
from minidb.catalog import Catalog
from minidb.disk_manager import DiskManager
from minidb.types import Column, ColumnType, Schema

INT, TEXT = ColumnType.INT, ColumnType.TEXT


def make_catalog():
    return Catalog(BufferPool(DiskManager(":memory:"), num_frames=16))


def users_schema():
    return Schema(
        [
            Column("id", INT, nullable=False, primary_key=True),
            Column("name", TEXT),
            Column("city", TEXT),
        ]
    )


def test_create_get_drop_table():
    cat = make_catalog()
    cat.create_table("users", users_schema())
    assert cat.has_table("users")
    assert cat.table_names() == ["users"]
    assert cat.get_table("users").name == "users"
    cat.drop_table("users")
    assert not cat.has_table("users")
    with pytest.raises(KeyError):
        cat.get_table("users")


def test_duplicate_table_rejected():
    cat = make_catalog()
    cat.create_table("t", users_schema())
    with pytest.raises(ValueError):
        cat.create_table("t", users_schema())


def test_pk_index_created_automatically():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    assert t.has_index("id")  # PK auto-indexed
    assert t.pk_column == "id"


def test_insert_and_pk_lookup():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    t.insert_row((1, "ada", "london"))
    t.insert_row((2, "grace", "nyc"))
    rid, row = t.get_by_pk(2)
    assert row == (2, "grace", "nyc")
    assert t.get_by_pk(99) is None
    assert t.stats.row_count == 2


def test_duplicate_pk_rejected():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    t.insert_row((1, "ada", "london"))
    with pytest.raises(DuplicateKeyError):
        t.insert_row((1, "other", "paris"))
    # the failed insert did not change row count
    assert t.stats.row_count == 1


def test_scan_returns_all_rows():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    rows = [(i, f"u{i}", "x") for i in range(10)]
    for r in rows:
        t.insert_row(r)
    scanned = sorted(row for _, row in t.scan())
    assert scanned == rows


def test_delete_by_key_updates_heap_and_index():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    t.insert_row((1, "ada", "london"))
    t.insert_row((2, "grace", "nyc"))
    assert t.delete_by_key(1) is True
    assert t.get_by_pk(1) is None        # gone from index
    assert t.stats.row_count == 1
    assert sorted(row for _, row in t.scan()) == [(2, "grace", "nyc")]
    assert t.delete_by_key(1) is False   # already gone


def test_secondary_index_built_from_existing_rows():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    for i in range(5):
        t.insert_row((i, f"u{i}", "london" if i % 2 == 0 else "nyc"))
    t.create_index("city")
    assert t.has_index("city")
    # the new index now tracks subsequent inserts too
    t.insert_row((5, "u5", "london"))
    londoners = t.index_lookup("city", "london")  # non-unique equality lookup
    assert len(londoners) == 4  # ids 0,2,4,5
    # the RIDs resolve to the expected rows
    ids = sorted(t.schema.decode(t.heap.get(r))[0] for r in londoners)
    assert ids == [0, 2, 4, 5]


def test_secondary_index_range_and_duplicate_delete():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    # three londoners, two new-yorkers
    for i, city in enumerate(["london", "london", "nyc", "london", "nyc"]):
        t.insert_row((i, f"u{i}", city))
    t.create_index("city")
    assert len(t.index_lookup("city", "london")) == 3
    # range over a sorted column prefix
    assert len(t.index_range("city", "london", "london")) == 3
    assert len(t.index_range("city", "a", "z")) == 5
    # deleting one duplicate leaves the others addressable
    assert t.delete_by_key(0) is True  # was a londoner
    assert len(t.index_lookup("city", "london")) == 2


def test_analyze_computes_stats():
    cat = make_catalog()
    t = cat.create_table("users", users_schema())
    for i in range(20):
        t.insert_row((i, f"u{i}", "london" if i < 5 else "nyc"))
    t.create_index("city")
    t.analyze()
    assert t.stats.row_count == 20
    assert t.stats.ndv["id"] == 20      # PK: all distinct
    assert t.stats.ndv["city"] == 2     # london / nyc
