"""Tests for the clock-sweep buffer pool (build step 4)."""

import pytest

from minidb.buffer_pool import BufferPool, BufferPoolFullError
from minidb.disk_manager import DiskManager


def make_pool(frames=3):
    return BufferPool(DiskManager(":memory:"), num_frames=frames)


def test_new_page_is_pinned_and_dirty():
    bp = make_pool()
    p = bp.new_page()
    # pinned: cannot be evicted; flush writes it because it's dirty
    bp.flush_page(p.page_id)
    assert bp.disk.writes == 1
    bp.unpin_page(p.page_id)


def test_fetch_hit_vs_miss_counters():
    bp = make_pool()
    p = bp.new_page()
    pid = p.page_id
    bp.unpin_page(pid)
    bp.flush_all()
    # First fetch after it's resident -> hit
    bp.fetch_page(pid)
    assert bp.hits == 1 and bp.misses == 0
    bp.unpin_page(pid)


def test_miss_loads_from_disk():
    dm = DiskManager(":memory:")
    bp = BufferPool(dm, num_frames=1)
    p = bp.new_page()
    pid = p.page_id
    bp.unpin_page(pid, dirty=True)
    bp.flush_all()
    # Evict pid by fetching enough other pages (pool has 1 frame)
    other = bp.new_page()
    bp.unpin_page(other.page_id)
    # Now pid is gone from cache -> fetching it is a miss + a disk read
    reads_before = dm.reads
    bp.fetch_page(pid)
    assert bp.misses >= 1
    assert dm.reads == reads_before + 1
    bp.unpin_page(pid)


def test_dirty_page_flushed_on_eviction():
    dm = DiskManager(":memory:")
    bp = BufferPool(dm, num_frames=1)
    p = bp.new_page()
    pid = p.page_id
    p.insert(b"data")
    bp.unpin_page(pid, dirty=True)
    writes_before = dm.writes
    # Force eviction of the dirty page by bringing in another page
    p2 = bp.new_page()
    bp.unpin_page(p2.page_id)
    assert dm.writes == writes_before + 1  # victim was flushed
    # And the data survived: fetch it back
    back = bp.fetch_page(pid)
    assert back.get(0) == b"data"
    bp.unpin_page(pid)


def test_pinned_pages_are_never_evicted():
    bp = make_pool(frames=2)
    a = bp.new_page()  # pinned
    b = bp.new_page()  # pinned
    # both frames pinned; a third page cannot find a victim
    with pytest.raises(BufferPoolFullError):
        bp.new_page()
    bp.unpin_page(a.page_id)
    bp.unpin_page(b.page_id)


def test_clock_sweep_second_chance_then_evicts():
    """Both pages enter with ref bits set; the sweep clears them (second chance)
    on the first pass and evicts the first frame reached with a cleared bit."""
    dm = DiskManager(":memory:")
    bp = BufferPool(dm, num_frames=2)
    a = bp.new_page(); bp.unpin_page(a.page_id)  # frame 0, ref set
    b = bp.new_page(); bp.unpin_page(b.page_id)  # frame 1, ref set
    # Pool full. Loading c sweeps: clears a's bit, clears b's bit, evicts a.
    c = bp.new_page(); bp.unpin_page(c.page_id)
    # b survived this round -> resident -> cache hit, no disk read
    reads = dm.reads
    bp.fetch_page(b.page_id); bp.unpin_page(b.page_id)
    assert dm.reads == reads
    # a was evicted -> fetching it must read from disk (a miss)
    r2, m2 = dm.reads, bp.misses
    bp.fetch_page(a.page_id); bp.unpin_page(a.page_id)
    assert dm.reads == r2 + 1
    assert bp.misses == m2 + 1


def test_unpin_errors():
    bp = make_pool()
    with pytest.raises(KeyError):
        bp.unpin_page(999)
    p = bp.new_page()
    bp.unpin_page(p.page_id)
    with pytest.raises(RuntimeError):
        bp.unpin_page(p.page_id)  # already fully unpinned


def test_hit_ratio():
    bp = make_pool()
    p = bp.new_page(); bp.unpin_page(p.page_id)
    bp.fetch_page(p.page_id); bp.unpin_page(p.page_id)  # hit
    assert 0.0 < bp.hit_ratio <= 1.0
