"""LSM-tree storage engine.

Writes land in an in-memory sorted MemTable and are flushed to immutable L0
SSTables once it fills, so the write path is sequential appends only. Reads
check newest-to-oldest (memtable, immutables, L0, deeper levels) and the first
hit wins; Bloom filters skip most SSTables without a disk read. Compaction
merges L0 into L1 to trade write amplification for read efficiency.
"""

import json
import os
from .sstable import SSTable, TOMBSTONE

L0_COMPACTION_TRIGGER = 4        # compact once L0 has this many tables


def _to_key(k):
    """Normalize keys to bytes with order-preserving encoding for ints."""
    if isinstance(k, bytes):
        return k
    if isinstance(k, int):
        # 8-byte big-endian, biased so signed ints keep natural order
        return (k + (1 << 63)).to_bytes(8, "big")
    return str(k).encode("utf-8")


class LSMEngine:
    def __init__(self, directory: str, memtable_limit: int = 1000,
                 auto_flush: bool = True):
        self.dir = directory
        os.makedirs(directory, exist_ok=True)
        self.memtable_limit = memtable_limit
        self.auto_flush = auto_flush
        self.manifest_path = os.path.join(directory, "manifest.json")
        self.memtable = {}                  # key:bytes -> value:bytes | TOMBSTONE
        self.immutables = []                # list of dicts pending flush
        self.levels = {0: [], 1: []}        # level -> list[SSTable] (L0 newest first)
        # cache the level order; avoids re-sorting this 2-key dict on every get.
        self._sorted_levels = sorted(self.levels)
        self._seq = 0                       # unique sstable file counter
        # metrics
        self.bytes_written = 0
        self.flushes = 0
        self.compactions = 0
        self.get_sstable_reads = 0
        self.get_bloom_skips = 0
        self._load_manifest()

    def put(self, key, value: bytes):
        self.memtable[_to_key(key)] = value
        if self.auto_flush and len(self.memtable) >= self.memtable_limit:
            self._rotate_and_flush()

    def delete(self, key):
        self.memtable[_to_key(key)] = TOMBSTONE
        if self.auto_flush and len(self.memtable) >= self.memtable_limit:
            self._rotate_and_flush()

    def get(self, key):
        k = _to_key(key)
        # 1) active memtable
        if k in self.memtable:
            v = self.memtable[k]
            return None if v is TOMBSTONE else v
        # 2) immutable memtables (newest first)
        for mt in reversed(self.immutables):
            if k in mt:
                v = mt[k]
                return None if v is TOMBSTONE else v
        # 3) L0 newest-first, then deeper levels
        for level in self._sorted_levels:
            tables = self.levels[level]
            ordered = reversed(tables) if level == 0 else tables
            for sst in ordered:
                self.get_sstable_reads += 1
                before = sst.bloom_skips
                found, value = sst.get(k)
                if sst.bloom_skips > before:
                    self.get_bloom_skips += 1
                if found:
                    return value            # value None => tombstone => deleted
        return None

    def _rotate_and_flush(self):
        self.immutables.append(self.memtable)
        self.memtable = {}
        self._flush_oldest_immutable()

    def _flush_oldest_immutable(self):
        if not self.immutables:
            return
        mt = self.immutables.pop(0)
        items = sorted(mt.items(), key=lambda kv: kv[0])
        sst = self._write_sstable(items)
        self.levels[0].append(sst)
        self.flushes += 1
        if len(self.levels[0]) >= L0_COMPACTION_TRIGGER:
            self.compact()
        else:
            self._persist_manifest()

    def flush(self):
        """Force-flush the active memtable (e.g., before benchmarking reads)."""
        if self.memtable:
            self.immutables.append(self.memtable)
            self.memtable = {}
        while self.immutables:
            self._flush_oldest_immutable()

    def _write_sstable(self, items):
        prefix = os.path.join(self.dir, f"sst_{self._seq:06d}")
        self._seq += 1
        sst = SSTable.write(prefix, items)
        self.bytes_written += sst.size_bytes
        return sst

    def compact(self):
        """Merge all L0 tables + L1 into one sorted L1 run (leveled compaction).

        Newer entries win; tombstones are dropped only at the bottom level (here
        L1, the deepest), since no older version can resurface beneath them.
        """
        sources = list(self.levels[0]) + list(self.levels[1])
        if not sources:
            return
        # Merge: iterate every source, newest precedence. L0 newest is last
        # appended; L1 is older than all L0. Build precedence list oldest->newest
        # so later writes overwrite earlier ones in the dict.
        ordered_oldest_first = list(self.levels[1]) + list(self.levels[0])
        merged = {}
        for sst in ordered_oldest_first:
            for k, v in sst.scan():
                merged[k] = v
        # We drop tombstones only at the deepest level; doing it earlier was the
        # easiest way to accidentally resurrect an older value while testing.
        items = [(k, v) for k, v in sorted(merged.items()) if v is not TOMBSTONE]
        new_l1 = self._write_sstable(items) if items else None
        # Publish the new manifest before deleting old files. If cleanup is
        # interrupted, reopen follows the manifest and orphaned files are ignored.
        self.levels[0] = []
        self.levels[1] = [new_l1] if new_l1 else []
        self.compactions += 1
        self._persist_manifest()
        for sst in sources:
            sst.remove_files()

    def scan(self):
        """Yield latest live (key, value) pairs in key order."""
        merged = {}
        for level in sorted(self.levels, reverse=True):
            for sst in self.levels[level]:
                for k, v in sst.scan():
                    merged[k] = v
        for mt in self.immutables:
            merged.update(mt)
        merged.update(self.memtable)
        for k, v in sorted(merged.items()):
            if v is not TOMBSTONE:
                yield k, v

    def _load_manifest(self):
        if not os.path.exists(self.manifest_path):
            self._seq = self._next_sequence_from_files()
            return
        with open(self.manifest_path, "r") as f:
            data = json.load(f)
        self.levels = {0: [], 1: []}
        for level, prefixes in data.get("levels", {}).items():
            self.levels[int(level)] = [
                SSTable.open(os.path.join(self.dir, prefix))
                for prefix in prefixes
            ]
        self._sorted_levels = sorted(self.levels)
        self._seq = max(data.get("next_sequence", 0), self._next_sequence_from_files())

    def _persist_manifest(self):
        data = {
            "next_sequence": self._seq,
            "levels": {
                str(level): [os.path.basename(sst.prefix) for sst in tables]
                for level, tables in self.levels.items()
            },
        }
        tmp = self.manifest_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, self.manifest_path)

    def _next_sequence_from_files(self):
        seq = 0
        for name in os.listdir(self.dir):
            if name.startswith("sst_") and name.endswith(".data"):
                try:
                    seq = max(seq, int(name.split("_")[1].split(".")[0]) + 1)
                except (ValueError, IndexError):
                    pass
        return seq

    def stats(self) -> dict:
        physical = sum(s.count for lvl in self.levels.values() for s in lvl)
        physical += sum(len(mt) for mt in self.immutables) + len(self.memtable)
        return {
            "memtable_entries": len(self.memtable),
            "immutable_memtables": len(self.immutables),
            "l0_tables": len(self.levels[0]),
            "l1_tables": len(self.levels[1]),
            "flushes": self.flushes,
            "compactions": self.compactions,
            "bytes_written": self.bytes_written,
            "physical_entries": physical,
            "get_sstable_reads": self.get_sstable_reads,
            "get_bloom_skips": self.get_bloom_skips,
        }
