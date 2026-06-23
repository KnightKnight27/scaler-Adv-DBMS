"""lsm.py — a Log-Structured Merge-tree storage engine (Track C extension).

Built alongside the heap engine, not replacing it. The LSM optimizes writes:

    write path:  put/delete -> append to WAL -> update in-memory MemTable
                 (when the MemTable is full) -> flush as an immutable SSTable
    read path:   MemTable (newest) -> SSTables newest..oldest, each gated by a
                 Bloom filter so most are skipped; first hit (value or tombstone) wins

Components:
  * MemTable    — an in-memory dict; the mutable write buffer.
  * SSTable     — an immutable, sorted on-disk run with an in-memory key->offset
                  index and a Bloom filter; values are read lazily by seek.
  * BloomFilter — compact probabilistic membership ("definitely not / maybe").
  * Compaction  — size-tiered: when SSTables pile up, merge them (newest wins,
                  tombstones dropped) into one, bounding space amplification.

Deletes are tombstones (a delete marker) so they can shadow older SSTable values.
"""

from __future__ import annotations

import hashlib
import math
import os
import struct
from typing import Iterator

# sentinel marking a deleted key inside MemTable / SSTable
TOMBSTONE = b"\x00__LSM_TOMBSTONE__\x00"

_U32 = struct.Struct("<I")


# =============================================================================
# Bloom filter
# =============================================================================


class BloomFilter:
    """A bit-array Bloom filter using double hashing (h_i = h1 + i*h2)."""

    def __init__(self, n_items: int, fp_rate: float = 0.01) -> None:
        n = max(n_items, 1)
        # optimal m (bits) and k (hashes) for target false-positive rate
        m = max(8, int(-(n * math.log(fp_rate)) / (math.log(2) ** 2)))
        self.m = m
        self.k = max(1, int((m / n) * math.log(2)))
        self.bits = bytearray((m + 7) // 8)

    def _hashes(self, key: bytes) -> Iterator[int]:
        digest = hashlib.sha256(key).digest()
        h1 = int.from_bytes(digest[:8], "little")
        h2 = int.from_bytes(digest[8:16], "little") | 1  # ensure odd
        for i in range(self.k):
            yield (h1 + i * h2) % self.m

    def add(self, key: bytes) -> None:
        for pos in self._hashes(key):
            self.bits[pos >> 3] |= 1 << (pos & 7)

    def __contains__(self, key: bytes) -> bool:
        return all(self.bits[pos >> 3] & (1 << (pos & 7)) for pos in self._hashes(key))


# =============================================================================
# SSTable (immutable sorted run on disk)
# =============================================================================


class SSTable:
    """An immutable sorted on-disk run.

    File layout: a sequence of records, each
        [keylen u32][key bytes][vallen u32][val bytes]
    written in ascending key order. A deleted key stores TOMBSTONE as its value.
    On open we scan once to build an in-memory index (key -> file offset) and a
    Bloom filter; values are then read lazily by seeking.
    """

    def __init__(self, path: str) -> None:
        self.path = path
        self.index: dict[bytes, int] = {}
        self._build_index()

    @classmethod
    def create(cls, path: str, sorted_items: list[tuple[bytes, bytes]]) -> "SSTable":
        with open(path, "wb") as f:
            for key, val in sorted_items:
                f.write(_U32.pack(len(key)))
                f.write(key)
                f.write(_U32.pack(len(val)))
                f.write(val)
        return cls(path)

    def _build_index(self) -> None:
        self.bloom = BloomFilter(max(self._count_records(), 1))
        with open(self.path, "rb") as f:
            while True:
                pos = f.tell()
                head = f.read(4)
                if not head:
                    break
                klen = _U32.unpack(head)[0]
                key = f.read(klen)
                vlen = _U32.unpack(f.read(4))[0]
                f.seek(vlen, os.SEEK_CUR)  # skip value
                self.index[key] = pos
                self.bloom.add(key)

    def _count_records(self) -> int:
        n = 0
        with open(self.path, "rb") as f:
            while True:
                head = f.read(4)
                if not head:
                    break
                klen = _U32.unpack(head)[0]
                f.seek(klen, os.SEEK_CUR)
                vlen = _U32.unpack(f.read(4))[0]
                f.seek(vlen, os.SEEK_CUR)
                n += 1
        return n

    def get(self, key: bytes) -> bytes | None:
        """Return the stored value (possibly TOMBSTONE), or None if absent here."""
        if key not in self.bloom:        # Bloom: "definitely not here" -> skip
            return None
        pos = self.index.get(key)
        if pos is None:                  # Bloom false positive
            return None
        with open(self.path, "rb") as f:
            f.seek(pos)
            klen = _U32.unpack(f.read(4))[0]
            f.read(klen)
            vlen = _U32.unpack(f.read(4))[0]
            return f.read(vlen)

    def items(self) -> Iterator[tuple[bytes, bytes]]:
        with open(self.path, "rb") as f:
            while True:
                head = f.read(4)
                if not head:
                    break
                klen = _U32.unpack(head)[0]
                key = f.read(klen)
                vlen = _U32.unpack(f.read(4))[0]
                yield key, f.read(vlen)

    def size_bytes(self) -> int:
        return os.path.getsize(self.path)


# =============================================================================
# LSM tree
# =============================================================================


class LSMTree:
    def __init__(
        self,
        directory: str,
        memtable_limit: int = 1000,
        compaction_threshold: int = 4,
    ) -> None:
        self.dir = directory
        os.makedirs(directory, exist_ok=True)
        self.memtable_limit = memtable_limit
        self.compaction_threshold = compaction_threshold
        self.memtable: dict[bytes, bytes] = {}
        self.sstables: list[SSTable] = []   # oldest first; newest last
        self._seq = 0
        self.wal_path = os.path.join(directory, "memtable.wal")
        self._wal = None
        self._open()

    # --- lifecycle ---------------------------------------------------------

    def _open(self) -> None:
        # load existing SSTables in sequence order
        names = sorted(
            n for n in os.listdir(self.dir) if n.endswith(".sst")
        )
        for n in names:
            self.sstables.append(SSTable(os.path.join(self.dir, n)))
            self._seq = max(self._seq, int(n.split(".")[0]) + 1)
        # replay the MemTable WAL (writes not yet flushed to an SSTable)
        if os.path.exists(self.wal_path):
            with open(self.wal_path, "rb") as f:
                while True:
                    head = f.read(4)
                    if not head:
                        break
                    klen = _U32.unpack(head)[0]
                    key = f.read(klen)
                    vlen = _U32.unpack(f.read(4))[0]
                    self.memtable[key] = f.read(vlen)
        self._wal = open(self.wal_path, "ab")

    # --- write path --------------------------------------------------------

    def _key(self, key) -> bytes:
        return key if isinstance(key, bytes) else str(key).encode("utf-8")

    def _val(self, val) -> bytes:
        return val if isinstance(val, bytes) else str(val).encode("utf-8")

    def _wal_append(self, key: bytes, val: bytes) -> None:
        self._wal.write(_U32.pack(len(key)) + key + _U32.pack(len(val)) + val)

    def put(self, key, value) -> None:
        k, v = self._key(key), self._val(value)
        self._wal_append(k, v)
        self.memtable[k] = v
        if len(self.memtable) >= self.memtable_limit:
            self.flush()

    def delete(self, key) -> None:
        k = self._key(key)
        self._wal_append(k, TOMBSTONE)
        self.memtable[k] = TOMBSTONE
        if len(self.memtable) >= self.memtable_limit:
            self.flush()

    # --- read path ---------------------------------------------------------

    def get(self, key):
        """Return the value bytes for key, or None if absent/deleted."""
        k = self._key(key)
        if k in self.memtable:
            v = self.memtable[k]
            return None if v == TOMBSTONE else v
        for sst in reversed(self.sstables):   # newest -> oldest
            v = sst.get(k)
            if v is not None:
                return None if v == TOMBSTONE else v
        return None

    def __contains__(self, key) -> bool:
        return self.get(key) is not None

    # --- flush + compaction ------------------------------------------------

    def flush(self) -> SSTable | None:
        """Write the MemTable to a new immutable SSTable and reset the WAL."""
        if not self.memtable:
            return None
        items = sorted(self.memtable.items())
        path = os.path.join(self.dir, f"{self._seq:08d}.sst")
        self._seq += 1
        sst = SSTable.create(path, items)
        self.sstables.append(sst)
        self.memtable.clear()
        # MemTable is now durable in the SSTable: reset the WAL
        self._wal.close()
        self._wal = open(self.wal_path, "wb")
        if len(self.sstables) >= self.compaction_threshold:
            self.compact()
        return sst

    def compact(self) -> SSTable | None:
        """Size-tiered full compaction: merge all SSTables (newest wins),
        dropping tombstones, into a single new SSTable."""
        if len(self.sstables) < 2:
            return None
        merged: dict[bytes, bytes] = {}
        for sst in self.sstables:             # oldest -> newest, newest overwrites
            for key, val in sst.items():
                merged[key] = val
        live = sorted((k, v) for k, v in merged.items() if v != TOMBSTONE)
        old_paths = [s.path for s in self.sstables]
        path = os.path.join(self.dir, f"{self._seq:08d}.sst")
        self._seq += 1
        new_sst = SSTable.create(path, live)
        self.sstables = [new_sst]
        for p in old_paths:                   # reclaim space
            os.remove(p)
        return new_sst

    # --- introspection (benchmarks/demos) ----------------------------------

    def items(self) -> Iterator[tuple[bytes, bytes]]:
        """Merged, sorted, newest-wins view across MemTable + all SSTables."""
        merged: dict[bytes, bytes] = {}
        for sst in self.sstables:
            for k, v in sst.items():
                merged[k] = v
        merged.update(self.memtable)
        for k in sorted(merged):
            if merged[k] != TOMBSTONE:
                yield k, merged[k]

    def stats(self) -> dict:
        return {
            "memtable_size": len(self.memtable),
            "num_sstables": len(self.sstables),
            "disk_bytes": sum(s.size_bytes() for s in self.sstables),
            "live_keys": sum(1 for _ in self.items()),
        }

    def close(self) -> None:
        if self._wal is not None:
            self._wal.close()
            self._wal = None
