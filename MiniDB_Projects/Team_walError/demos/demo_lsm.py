"""demo_lsm.py — Track C: LSM-tree write/read paths, Bloom filters, compaction."""

import tempfile

import _demo
from _demo import banner, show, step

from minidb.lsm import LSMTree


def main() -> None:
    banner("LSM-TREE (Track C): MemTable -> SSTables, Bloom filters, compaction")
    tmpdir = tempfile.mkdtemp(prefix="minidb_lsm_")
    # small limits so flushes/compaction happen during the demo
    lsm = LSMTree(tmpdir, memtable_limit=4, compaction_threshold=3)

    step("Writes go to the in-memory MemTable (+ a WAL) — no random disk I/O")
    for i in range(3):
        lsm.put(f"user:{i}", f"v{i}".encode())
    show("MemTable size", lsm.stats()["memtable_size"])
    show("get('user:1')", lsm.get("user:1"))

    step("Crossing the MemTable limit flushes an immutable, sorted SSTable")
    for i in range(3, 10):
        lsm.put(f"user:{i}", f"v{i}".encode())
    show("SSTables on disk", lsm.stats()["num_sstables"])

    step("Reads check MemTable, then SSTables newest->oldest (Bloom-filtered)")
    show("get('user:7')", lsm.get("user:7"))
    show("get('user:404') (absent)", lsm.get("user:404"))
    sst = lsm.sstables[0]
    show("bloom: 'user:404' maybe present?", "user:404".encode() in sst.bloom)

    step("Overwrite + delete create newer versions / tombstones")
    lsm.put("user:1", b"UPDATED")
    lsm.delete("user:2")
    show("get('user:1') -> newest wins", lsm.get("user:1"))
    show("get('user:2') -> tombstoned", lsm.get("user:2"))

    step("Compaction merges SSTables (newest wins) and drops tombstones")
    for i in range(10, 20):
        lsm.put(f"user:{i}", f"v{i}".encode())  # triggers flushes + compaction
    s = lsm.stats()
    show("SSTables after compaction", s["num_sstables"])
    show("live keys", s["live_keys"])
    show("on-disk bytes", s["disk_bytes"])
    show("user:2 still deleted after compaction", lsm.get("user:2") is None)
    lsm.close()

    print("\nTakeaway: append-only writes + background compaction make the LSM great")
    print("for write-heavy workloads; Bloom filters keep multi-SSTable reads cheap.")


if __name__ == "__main__":
    main()
