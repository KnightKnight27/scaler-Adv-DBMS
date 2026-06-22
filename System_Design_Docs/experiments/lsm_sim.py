#!/usr/bin/env python3
"""
Minimal LSM-tree simulator — measures the three amplifications that define
RocksDB's behaviour: write, read, and space amplification.

It models the real RocksDB write path:
    writes -> in-memory MemTable -> flush to an L0 SSTable -> leveled compaction

We count actual bytes written to "disk" (every flush + every compaction rewrite),
SSTables touched per point lookup (with and without a Bloom filter), and bytes
held on disk vs live user data. No real I/O — just honest accounting of the
same operations RocksDB performs, so the amplification ratios are meaningful.
"""
import random

KEY_SPACE   = 20_000     # distinct keys (so updates overwrite -> dead versions)
N_WRITES    = 200_000    # total user writes (key,value) pairs
ENTRY_BYTES = 100        # bytes per entry (key+value+overhead)
MEMTABLE    = 2_000      # entries held in memory before a flush
LEVEL_RATIO = 10         # each level ~10x the previous (RocksDB default)
random.seed(42)

class Stats:
    def __init__(self):
        self.user_bytes = 0      # logical bytes the user wrote
        self.disk_writes = 0     # physical bytes written (flush + compaction)

def run(compaction):
    """compaction in {'none','leveled'}; returns metrics dict."""
    st = Stats()
    # levels: list of "SSTables"; each SSTable is a dict{key:val}. L0 is a list of tables.
    L0 = []
    levels = {1: {}, 2: {}, 3: {}}   # leveled: one sorted run per level (merged)
    memtable = {}

    def flush():
        # MemTable -> new L0 SSTable. Bytes written = size of the table.
        nonlocal memtable
        if not memtable:
            return
        table = dict(memtable)
        st.disk_writes += len(table) * ENTRY_BYTES
        L0.append(table)
        memtable = {}
        if compaction == 'leveled':
            compact()

    def compact():
        # Push L0 down through levels when a level exceeds its size budget.
        # Merging rewrites the merged data -> that rewrite IS write amplification.
        while len(L0) >= 4:                       # L0 trigger (RocksDB default = 4)
            merged = {}
            for t in L0:
                merged.update(t)                  # newer overwrites older
            L0.clear()
            merged.update(levels[1])
            st.disk_writes += len(merged) * ENTRY_BYTES
            levels[1] = merged
            for lvl in (1, 2):
                budget = MEMTABLE * (LEVEL_RATIO ** lvl)
                if len(levels[lvl]) > budget:
                    nxt = dict(levels[lvl + 1])
                    nxt.update(levels[lvl])
                    st.disk_writes += len(nxt) * ENTRY_BYTES
                    levels[lvl + 1] = nxt
                    levels[lvl] = {}

    # ---- write path ----
    for _ in range(N_WRITES):
        k = random.randint(1, KEY_SPACE)
        memtable[k] = _
        st.user_bytes += ENTRY_BYTES
        if len(memtable) >= MEMTABLE:
            flush()
    flush()

    # ---- count runs (for read amplification) ----
    runs = len(L0) + sum(1 for lvl in levels.values() if lvl)

    # ---- space: physical live bytes on disk vs logical live data ----
    on_disk_entries = sum(len(t) for t in L0) + sum(len(lvl) for lvl in levels.values())
    live_keys = set()
    for lvl in levels.values():
        live_keys |= lvl.keys()
    for t in L0:
        live_keys |= t.keys()
    live_entries = len(live_keys)

    # ---- read amplification: SSTables probed per lookup ----
    # Without bloom filters you may probe every run; with them you skip runs
    # that certainly don't hold the key (1% false-positive rate assumed).
    trials = 5_000
    probes_nobloom = probes_bloom = 0
    all_runs = [t for t in L0] + [lvl for lvl in levels.values() if lvl]
    for _ in range(trials):
        k = random.randint(1, KEY_SPACE)
        for run_tbl in all_runs:
            probes_nobloom += 1
            if k in run_tbl:
                probes_bloom += 1
                break
            else:
                probes_bloom += 0.01          # 1% bloom false-positive probe
    return {
        'compaction': compaction,
        'runs_to_search': runs,
        'write_amp': round(st.disk_writes / st.user_bytes, 2),
        'space_amp': round(on_disk_entries / live_entries, 2),
        'read_probes_per_lookup_nobloom': round(probes_nobloom / trials, 2),
        'read_probes_per_lookup_bloom':   round(probes_bloom / trials, 2),
    }

def show(m):
    print(f"  compaction = {m['compaction']}")
    print(f"    sorted runs to search per read : {m['runs_to_search']}")
    print(f"    WRITE amplification            : {m['write_amp']}x  (disk bytes / user bytes)")
    print(f"    SPACE amplification            : {m['space_amp']}x  (on-disk entries / live entries)")
    print(f"    READ probes/lookup (no bloom)  : {m['read_probes_per_lookup_nobloom']}")
    print(f"    READ probes/lookup (w/ bloom)  : {m['read_probes_per_lookup_bloom']}")
    print()

if __name__ == '__main__':
    print(f"LSM simulator: {N_WRITES:,} writes over {KEY_SPACE:,} keys "
          f"(={N_WRITES//KEY_SPACE}x overwrite), memtable={MEMTABLE}\n")
    print("=== No compaction (writes are cheap, reads/space blow up) ===")
    show(run('none'))
    print("=== Leveled compaction (RocksDB default — pays writes to keep reads/space low) ===")
    show(run('leveled'))
