"""demo_storage.py — pages, the disk manager, and the clock-sweep buffer pool."""

import _demo
from _demo import banner, step, show

from minidb.buffer_pool import BufferPool
from minidb.disk_manager import DiskManager
from minidb.page import Page


def main() -> None:
    banner("STORAGE LAYER: slotted pages + disk manager + buffer pool")

    step("Allocate pages in a single DB file and write slotted records")
    dm = DiskManager(":memory:")
    pid = dm.allocate_page()
    page = Page(pid)
    s0 = page.insert(b"alice")
    s1 = page.insert(b"bob")
    dm.write_page(pid, page.to_bytes())
    show("page id", pid)
    show("slots written", [s0, s1])
    show("free space left in page (bytes)", page.free_space)

    step("Read the page back from disk and recover the records")
    back = Page(pid, dm.read_page(pid))
    show("slot 0", back.get(s0))
    show("slot 1", back.get(s1))

    step("Tombstone delete keeps record ids (RIDs) stable")
    back.delete(s0)
    show("slot 0 after delete", back.get(s0))
    show("slot 1 unchanged", back.get(s1))
    show("live slots", back.live_slots())

    banner("BUFFER POOL: clock-sweep eviction with hit/miss accounting")
    dm2 = DiskManager(":memory:")
    bp = BufferPool(dm2, num_frames=2)  # tiny pool to force eviction

    step("Bring 2 pages into a 2-frame pool (both are misses)")
    a = bp.new_page(); bp.unpin_page(a.page_id)
    b = bp.new_page(); bp.unpin_page(b.page_id)
    show("hits / misses", f"{bp.hits} / {bp.misses}")

    step("Re-access page A (a cache HIT, sets its reference bit)")
    bp.fetch_page(a.page_id); bp.unpin_page(a.page_id)
    show("hits / misses", f"{bp.hits} / {bp.misses}")

    step("Load a 3rd page: clock-sweep must evict a victim")
    c = bp.new_page(); bp.unpin_page(c.page_id)
    reads_before = dm2.reads
    bp.fetch_page(b.page_id); bp.unpin_page(b.page_id)
    evicted_b = dm2.reads > reads_before
    show("page B was evicted (had to re-read from disk)", evicted_b)
    show("final hit ratio", f"{bp.hit_ratio:.2%}")
    print("\nTakeaway: pages move between disk and a bounded memory pool; the clock")
    print("hand approximates LRU using one reference bit per frame.")


if __name__ == "__main__":
    main()
