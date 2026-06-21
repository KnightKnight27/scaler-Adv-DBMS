"""Tests for the schema codec and the system catalog."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb.catalog.catalog import Catalog
from minidb.catalog.schema import Column, DataType, Schema
from minidb.storage.buffer_pool import BufferPool
from minidb.storage.disk_manager import DiskManager


def _schema():
    return Schema([
        Column("id", DataType.INT, nullable=False),
        Column("name", DataType.TEXT),
        Column("score", DataType.FLOAT),
        Column("active", DataType.BOOL),
    ])


def test_tuple_roundtrip_with_nulls():
    s = _schema()
    row = {"id": 7, "name": "alice", "score": 9.5, "active": True}
    assert s.deserialize(s.serialize(row)) == row
    row2 = {"id": 8, "name": None, "score": None, "active": False}
    assert s.deserialize(s.serialize(row2)) == row2


def test_not_null_enforced():
    s = _schema()
    try:
        s.serialize({"id": None, "name": "x", "score": 1.0, "active": True})
        assert False, "expected NOT NULL violation"
    except ValueError:
        pass


def test_coerce_types():
    s = _schema()
    assert s.coerce("id", "42") == 42
    assert s.coerce("score", "3.5") == 3.5
    assert s.coerce("active", "true") is True


def _make(tmp_path):
    dm = DiskManager(str(tmp_path / "t.db"))
    bp = BufferPool(dm, pool_size=16)
    cat = Catalog(bp, str(tmp_path / "t.catalog.json"))
    return dm, bp, cat


def test_catalog_create_insert_index_reload(tmp_path):
    dm, bp, cat = _make(tmp_path)
    cols = [Column("id", DataType.INT, nullable=False), Column("city", DataType.TEXT)]
    t = cat.create_table("users", cols, pk_column="id")

    # Insert rows straight through the heap (no txn) and maintain the PK index.
    pk = t.primary_index()
    for i in range(20):
        rid = t.heap.insert(t.schema.serialize({"id": i, "city": f"c{i % 3}"}))
        pk.tree.insert(i, rid)
    cat.create_index("users", "city", unique=False)

    assert len(pk.tree) == 20
    assert pk.tree.search(5)  # present
    city_idx = t.index_on("city")
    assert len(city_idx.tree.search("c0")) == 7  # 0,3,6,9,12,15,18

    # Persist + reload into a fresh catalog, then rebuild indexes from the heap.
    cat.persist()
    bp.flush_all()

    dm2 = DiskManager(str(tmp_path / "t.db"))
    bp2 = BufferPool(dm2, pool_size=16)
    cat2 = Catalog(bp2, str(tmp_path / "t.catalog.json"))
    cat2.load()
    cat2.rebuild_all_indexes()

    t2 = cat2.get_table("users")
    assert t2.pk_column == "id"
    assert len(t2.primary_index().tree) == 20
    assert len(t2.index_on("city").tree.search("c1")) == 7  # 1,4,7,10,13,16,19
    rows = sorted(t2.schema.deserialize(rec)["id"] for _, rec in t2.heap.scan())
    assert rows == list(range(20))
