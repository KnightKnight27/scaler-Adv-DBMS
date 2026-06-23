"""Tests for the LSM-tree storage engine (build step 13, Track C extension)."""

import random

import pytest

from minidb.lsm import BloomFilter, LSMTree, SSTable, TOMBSTONE


# --- Bloom filter ------------------------------------------------------------


def test_bloom_no_false_negatives():
    bf = BloomFilter(1000)
    keys = [f"key{i}".encode() for i in range(1000)]
    for k in keys:
        bf.add(k)
    for k in keys:
        assert k in bf  # never a false negative


def test_bloom_false_positive_rate_is_low():
    bf = BloomFilter(1000, fp_rate=0.01)
    for i in range(1000):
        bf.add(f"present{i}".encode())
    fps = sum(1 for i in range(5000) if f"absent{i}".encode() in bf)
    assert fps / 5000 < 0.05  # comfortably below 5%


# --- SSTable -----------------------------------------------------------------


def test_sstable_roundtrip_and_lazy_get(tmp_path):
    path = str(tmp_path / "x.sst")
    items = [(f"k{i:03d}".encode(), f"v{i}".encode()) for i in range(20)]
    sst = SSTable.create(path, items)
    assert sst.get(b"k005") == b"v5"
    assert sst.get(b"k019") == b"v19"
    assert sst.get(b"missing") is None
    assert list(sst.items()) == items


def test_sstable_reopen_rebuilds_index(tmp_path):
    path = str(tmp_path / "x.sst")
    SSTable.create(path, [(b"a", b"1"), (b"b", b"2")])
    reopened = SSTable(path)
    assert reopened.get(b"a") == b"1"
    assert reopened.get(b"b") == b"2"


# --- LSM tree: write/read paths ---------------------------------------------


def test_put_get_from_memtable(tmp_path):
    lsm = LSMTree(str(tmp_path))
    lsm.put("a", b"1")
    lsm.put("b", b"2")
    assert lsm.get("a") == b"1"
    assert lsm.get("missing") is None
    lsm.close()


def test_overwrite_returns_newest(tmp_path):
    lsm = LSMTree(str(tmp_path), memtable_limit=3)
    lsm.put("k", b"old")
    lsm.put("k", b"new")
    assert lsm.get("k") == b"new"
    lsm.close()


def test_delete_tombstone(tmp_path):
    lsm = LSMTree(str(tmp_path))
    lsm.put("k", b"v")
    lsm.delete("k")
    assert lsm.get("k") is None
    assert "k" not in lsm
    lsm.close()


def test_flush_creates_sstable_and_reads_still_work(tmp_path):
    lsm = LSMTree(str(tmp_path), memtable_limit=5, compaction_threshold=100)
    for i in range(12):
        lsm.put(f"k{i:02d}", f"v{i}".encode())
    assert lsm.stats()["num_sstables"] >= 2  # flushed at least twice
    for i in range(12):
        assert lsm.get(f"k{i:02d}") == f"v{i}".encode()
    lsm.close()


def test_read_newest_sstable_wins_over_older(tmp_path):
    lsm = LSMTree(str(tmp_path), memtable_limit=2, compaction_threshold=100)
    lsm.put("k", b"v1")
    lsm.put("x", b"x")   # triggers flush (limit=2): SSTable0 has k=v1
    lsm.put("k", b"v2")
    lsm.put("y", b"y")   # flush: SSTable1 has k=v2
    assert lsm.get("k") == b"v2"   # newest SSTable wins
    lsm.close()


def test_delete_shadows_older_sstable(tmp_path):
    lsm = LSMTree(str(tmp_path), memtable_limit=2, compaction_threshold=100)
    lsm.put("k", b"v")
    lsm.put("a", b"a")   # flush SSTable0: k=v
    lsm.delete("k")
    lsm.put("b", b"b")   # flush SSTable1: k=tombstone
    assert lsm.get("k") is None
    lsm.close()


# --- compaction --------------------------------------------------------------


def test_compaction_merges_and_drops_tombstones(tmp_path):
    lsm = LSMTree(str(tmp_path), memtable_limit=2, compaction_threshold=3)
    lsm.put("a", b"1"); lsm.put("b", b"1")   # flush -> sst0
    lsm.put("a", b"2"); lsm.put("c", b"1")   # flush -> sst1
    lsm.delete("b"); lsm.put("d", b"1")      # flush -> sst2 -> triggers compaction
    assert lsm.stats()["num_sstables"] == 1  # merged into one
    assert lsm.get("a") == b"2"   # newest value kept
    assert lsm.get("b") is None   # tombstone dropped
    assert lsm.get("c") == b"1"
    assert lsm.get("d") == b"1"
    # tombstone for b should not remain in the merged run
    keys = {k for k, _ in lsm.sstables[0].items()}
    assert b"b" not in keys
    lsm.close()


# --- durability of the MemTable WAL -----------------------------------------


def test_memtable_wal_recovers_unflushed_writes(tmp_path):
    d = str(tmp_path)
    lsm = LSMTree(d, memtable_limit=1000)  # high limit -> no flush
    lsm.put("k1", b"v1")
    lsm.put("k2", b"v2")
    # simulate crash: close without flushing to an SSTable
    lsm._wal.close()
    lsm._wal = None

    lsm2 = LSMTree(d)  # reopen -> replays MemTable WAL
    assert lsm2.get("k1") == b"v1"
    assert lsm2.get("k2") == b"v2"
    lsm2.close()


# --- model-based fuzz vs a dict ---------------------------------------------


@pytest.mark.parametrize("seed", [1, 7, 42])
def test_model_based_random_ops(seed, tmp_path):
    rng = random.Random(seed)
    lsm = LSMTree(str(tmp_path / str(seed)), memtable_limit=8, compaction_threshold=3)
    model: dict[str, bytes] = {}
    for _ in range(400):
        k = f"k{rng.randrange(30)}"
        if rng.random() < 0.7:
            v = f"v{rng.randrange(1000)}".encode()
            lsm.put(k, v)
            model[k] = v
        else:
            lsm.delete(k)
            model.pop(k, None)
        probe = f"k{rng.randrange(30)}"
        assert lsm.get(probe) == model.get(probe)
    assert dict((k.decode(), v) for k, v in lsm.items()) == model
    lsm.close()
