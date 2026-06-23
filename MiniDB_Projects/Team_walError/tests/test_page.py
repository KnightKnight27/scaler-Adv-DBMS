"""Tests for the slotted page (build step 2)."""

import pytest

from minidb.constants import PAGE_SIZE
from minidb.page import HEADER_SIZE, Page


def test_empty_page_geometry():
    p = Page(0)
    assert p.num_slots == 0
    assert p.free_end == PAGE_SIZE
    assert p.free_space == PAGE_SIZE - HEADER_SIZE
    assert p.to_bytes() == bytes(p.data)
    assert len(p.to_bytes()) == PAGE_SIZE


def test_insert_get_roundtrip_and_slot_numbers():
    p = Page(1)
    s0 = p.insert(b"hello")
    s1 = p.insert(b"world!!")
    assert (s0, s1) == (0, 1)
    assert p.get(0) == b"hello"
    assert p.get(1) == b"world!!"
    assert p.num_slots == 2
    assert p.live_slots() == [0, 1]


def test_records_grow_backward_free_space_shrinks():
    p = Page(2)
    before = p.free_space
    p.insert(b"abc")
    # consumed = record (3) + slot (4) = 7 bytes
    assert p.free_space == before - 7
    assert p.free_end == PAGE_SIZE - 3


def test_delete_is_tombstone_rid_stable():
    p = Page(3)
    p.insert(b"keep-0")
    p.insert(b"drop-1")
    p.insert(b"keep-2")
    assert p.delete(1) is True
    assert p.get(1) is None
    assert p.is_deleted(1) is True
    # slot numbers of survivors are unchanged (RID stability)
    assert p.get(0) == b"keep-0"
    assert p.get(2) == b"keep-2"
    assert p.live_slots() == [0, 2]
    # double-delete and out-of-range are False
    assert p.delete(1) is False
    assert p.delete(99) is False


def test_get_out_of_range_returns_none():
    p = Page(4)
    assert p.get(0) is None
    assert p.get(-1) is None


def test_insert_returns_none_when_full():
    p = Page(5)
    big = b"x" * (PAGE_SIZE // 3)
    assert p.insert(big) == 0
    assert p.insert(big) == 1
    # third third-page record cannot fit alongside header + 3 slots
    assert p.insert(big) is None
    assert p.num_slots == 2  # failed insert did not add a slot


def test_serialization_roundtrip_preserves_records():
    p = Page(6)
    p.insert(b"alpha")
    p.insert(b"beta")
    p.delete(0)
    raw = p.to_bytes()
    p2 = Page(6, raw)
    assert p2.get(0) is None
    assert p2.get(1) == b"beta"
    assert p2.num_slots == 2
    assert p2.live_slots() == [1]


def test_items_yields_live_records():
    p = Page(7)
    p.insert(b"a")
    p.insert(b"b")
    p.insert(b"c")
    p.delete(1)
    assert list(p.items()) == [(0, b"a"), (2, b"c")]


def test_bad_page_size_rejected():
    with pytest.raises(ValueError):
        Page(0, b"too-short")
