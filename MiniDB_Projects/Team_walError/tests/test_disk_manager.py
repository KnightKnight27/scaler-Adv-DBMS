"""Tests for the disk manager (build step 3)."""

import pytest

from minidb.constants import PAGE_SIZE
from minidb.disk_manager import DiskManager
from minidb.page import Page


def test_allocate_grows_file_one_page_at_a_time():
    dm = DiskManager(":memory:")
    assert dm.num_pages == 0
    assert dm.allocate_page() == 0
    assert dm.allocate_page() == 1
    assert dm.num_pages == 2


def test_fresh_page_is_zeroed():
    dm = DiskManager(":memory:")
    pid = dm.allocate_page()
    assert dm.read_page(pid) == bytes(PAGE_SIZE)


def test_write_then_read_roundtrip():
    dm = DiskManager(":memory:")
    pid = dm.allocate_page()
    page = Page(pid)
    page.insert(b"durable-record")
    dm.write_page(pid, page.to_bytes())
    back = Page(pid, dm.read_page(pid))
    assert back.get(0) == b"durable-record"


def test_out_of_range_and_bad_size():
    dm = DiskManager(":memory:")
    with pytest.raises(ValueError):
        dm.read_page(0)  # nothing allocated yet
    pid = dm.allocate_page()
    with pytest.raises(ValueError):
        dm.write_page(pid, b"short")  # not PAGE_SIZE


def test_io_counters_increment():
    dm = DiskManager(":memory:")
    pid = dm.allocate_page()
    dm.write_page(pid, bytes(PAGE_SIZE))
    dm.write_page(pid, bytes(PAGE_SIZE))
    dm.read_page(pid)
    assert dm.writes == 2
    assert dm.reads == 1


def test_persistence_across_reopen(tmp_path):
    path = str(tmp_path / "test.db")
    dm = DiskManager(path)
    pid = dm.allocate_page()
    page = Page(pid)
    page.insert(b"survives-restart")
    dm.write_page(pid, page.to_bytes())
    dm.close()

    dm2 = DiskManager(path)  # reopen the same file
    assert dm2.num_pages == 1
    back = Page(pid, dm2.read_page(pid))
    assert back.get(0) == b"survives-restart"
    dm2.close()
