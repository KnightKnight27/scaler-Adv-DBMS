"""Tests for the heap file (build step 5)."""

import pytest

from minidb.buffer_pool import BufferPool
from minidb.disk_manager import DiskManager
from minidb.heap import RID, HeapFile


def make_heap(frames=8):
    return HeapFile(BufferPool(DiskManager(":memory:"), num_frames=frames))


def test_rid_serialization_roundtrip():
    r = RID(page_id=7, slot=3)
    assert RID.from_bytes(r.to_bytes()) == r


def test_insert_get_roundtrip():
    h = make_heap()
    rid = h.insert(b"alpha")
    assert h.get(rid) == b"alpha"


def test_multiple_inserts_unique_rids():
    h = make_heap()
    rids = [h.insert(f"row-{i}".encode()) for i in range(50)]
    assert len(set(rids)) == 50
    for i, rid in enumerate(rids):
        assert h.get(rid) == f"row-{i}".encode()


def test_insert_spills_to_new_pages():
    h = make_heap()
    big = b"y" * 1000  # ~4 per 4KB page
    for _ in range(20):
        h.insert(big)
    assert len(h.page_ids) > 1  # spilled across multiple pages


def test_delete_then_get_returns_none():
    h = make_heap()
    rid = h.insert(b"to-delete")
    assert h.delete(rid) is True
    assert h.get(rid) is None
    assert h.delete(rid) is False  # idempotent-ish: already gone


def test_delete_unknown_rid():
    h = make_heap()
    assert h.delete(RID(999, 0)) is False
    assert h.get(RID(999, 0)) is None


def test_scan_yields_live_records_only():
    h = make_heap()
    rids = [h.insert(f"r{i}".encode()) for i in range(5)]
    h.delete(rids[2])
    scanned = {rid: rec for rid, rec in h.scan()}
    assert rids[2] not in scanned
    assert len(scanned) == 4
    assert scanned[rids[0]] == b"r0"
    assert len(h) == 4


def test_update_in_place_same_length():
    h = make_heap()
    rid = h.insert(b"12345")
    assert h.update_in_place(rid, b"ABCDE") is True
    assert h.get(rid) == b"ABCDE"
    # different length is refused
    assert h.update_in_place(rid, b"longer-value") is False


def test_record_too_large_for_page():
    h = make_heap()
    with pytest.raises(ValueError, match="too large"):
        h.insert(b"z" * 5000)  # bigger than a 4KB page


def test_heap_survives_via_persisted_page_ids(tmp_path):
    """The heap's page list + buffer flush make a table reopenable."""
    path = str(tmp_path / "heap.db")
    dm = DiskManager(path)
    bp = BufferPool(dm, num_frames=8)
    h = HeapFile(bp)
    rid = h.insert(b"persisted")
    saved_pages = list(h.page_ids)
    bp.flush_all()
    dm.close()

    # reopen with the persisted page list (this is what the catalog stores)
    dm2 = DiskManager(path)
    bp2 = BufferPool(dm2, num_frames=8)
    h2 = HeapFile(bp2, page_ids=saved_pages)
    assert h2.get(rid) == b"persisted"
    dm2.close()
