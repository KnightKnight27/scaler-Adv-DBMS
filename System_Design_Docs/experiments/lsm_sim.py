"""A faithful (if simplified) LSM-tree simulator to MEASURE the three amplifications
that define RocksDB's design space: write, read, and space amplification.

Models: MemTable -> flush to L0 SSTables -> leveled compaction into L1..Ln with a
size fanout. SSTables are (min_key, max_key, {key:seqno}) and levels >=1 are kept
non-overlapping, exactly like LevelDB/RocksDB leveled compaction. We then contrast
'leveled' vs 'tiered' (size-tiered) compaction to show the amplification trade-off,
and measure how Bloom filters cut read amplification. Numbers below are MEASURED by
running this file, not quoted."""
import random, bisect
random.seed(7)

VAL_BYTES = 100  # fixed value size so bytes == entries * 100

class SSTable:
    __slots__ = ("data", "lo", "hi")
    def __init__(self, items):           # items: dict key->seq
        self.data = items
        ks = items.keys()
        self.lo, self.hi = min(ks), max(ks)
    def __len__(self): return len(self.data)
    def covers(self, k): return self.lo <= k <= self.hi

class LSM:
    def __init__(self, memtable_limit=2000, fanout=10, l0_trigger=4,
                 sst_size=2000, strategy="leveled", bloom_fp=0.01):
        self.memtable = {}
        self.levels = [[]]                # levels[0] = list of (overlapping) L0 SSTables
        self.mt_limit = memtable_limit
        self.fanout = fanout
        self.l0_trigger = l0_trigger
        self.sst_size = sst_size
        self.strategy = strategy
        self.bloom_fp = bloom_fp
        self.seq = 0
        self.bytes_ingested = 0
        self.bytes_written = 0            # flush + compaction writes (to "disk")

    def put(self, k):
        self.seq += 1
        self.memtable[k] = self.seq
        self.bytes_ingested += VAL_BYTES
        if len(self.memtable) >= self.mt_limit:
            self._flush()

    def _write_run(self, items):          # account a run written to disk
        self.bytes_written += len(items) * VAL_BYTES

    def _flush(self):
        if not self.memtable: return
        sst = SSTable(dict(self.memtable))
        self._write_run(sst.data)
        self.levels[0].append(sst)
        self.memtable.clear()
        self._maybe_compact()

    def _ensure_level(self, i):
        while len(self.levels) <= i: self.levels.append([])

    def _budget(self, i):                 # entry budget for level i>=1
        return self.sst_size * (self.fanout ** i)

    def _split(self, merged):             # dict -> list of non-overlapping SSTables, sorted
        keys = sorted(merged)
        out = []
        for j in range(0, len(keys), self.sst_size):
            chunk = keys[j:j+self.sst_size]
            out.append(SSTable({k: merged[k] for k in chunk}))
        return out

    def _maybe_compact(self):
        if self.strategy == "leveled":
            self._compact_leveled()
        else:
            self._compact_tiered()

    def _compact_leveled(self):
        # L0 -> L1 when too many overlapping L0 files
        if len(self.levels[0]) >= self.l0_trigger:
            self._ensure_level(1)
            merged = {}
            for sst in self.levels[1]:        # L1 is non-overlapping; include all overlapped
                merged.update(sst.data)
            for sst in self.levels[0]:        # newer L0 overrides on key collision (higher seq)
                for k, s in sst.data.items():
                    if merged.get(k, -1) < s: merged[k] = s
            self.levels[0] = []
            self.levels[1] = self._split(merged)
            self._write_run(merged)
        # cascade: push one SSTable's worth down when a level exceeds its budget
        i = 1
        while i < len(self.levels):
            if len(self.levels) <= i: break
            total = sum(len(s) for s in self.levels[i])
            if total > self._budget(i) and self.levels[i]:
                self._ensure_level(i + 1)
                victim = self.levels[i].pop(0)            # one file from Li
                merged = dict(victim.data)
                keep = []
                for sst in self.levels[i + 1]:            # overlapping Li+1 files
                    if sst.lo <= victim.hi and sst.hi >= victim.lo:
                        merged.update(sst.data)
                    else:
                        keep.append(sst)
                newfiles = self._split(merged)
                self.levels[i + 1] = sorted(keep + newfiles, key=lambda s: s.lo)
                self._write_run(merged)
                i = 1                                     # re-check from L1 after a move
            else:
                i += 1

    def _compact_tiered(self):
        # size-tiered: when a level accumulates >= fanout runs, merge them into ONE run
        # at the next level. Runs may overlap -> more runs to read, less rewriting.
        i = 0
        while i < len(self.levels):
            trig = self.l0_trigger if i == 0 else self.fanout
            if len(self.levels[i]) >= trig:
                self._ensure_level(i + 1)
                merged = {}
                for sst in self.levels[i]:
                    for k, s in sst.data.items():
                        if merged.get(k, -1) < s: merged[k] = s
                self.levels[i] = []
                self.levels[i + 1].append(SSTable(merged))
                self._write_run(merged)
                i = 0
            else:
                i += 1

    # ---- measurements ----
    def live_keys(self):
        seen = {}
        for k, s in self.memtable.items(): seen[k] = max(seen.get(k, -1), s)
        for lvl in self.levels:
            for sst in lvl:
                for k, s in sst.data.items():
                    if seen.get(k, -1) < s: seen[k] = s
        return seen

    def total_entries(self):
        return sum(len(s) for lvl in self.levels for s in lvl) + len(self.memtable)

    def write_amp(self):
        return self.bytes_written / self.bytes_ingested

    def space_amp(self):
        return self.total_entries() / max(1, len(self.live_keys()))

    def read_amp(self, sample, use_bloom):
        # count SSTables whose key-range covers the lookup key and that we must open.
        # Bloom filter lets us skip a covering SSTable that lacks the key (except FP).
        touched = 0
        for k in sample:
            if k in self.memtable:
                touched += 1; continue
            for sst in reversed(self.levels[0]):          # L0: newest first, all overlapping
                if sst.covers(k):
                    if k in sst.data: touched += 1; break
                    touched += (self.bloom_fp if use_bloom else 1)
            else:
                for i in range(1, len(self.levels)):
                    lvl = self.levels[i]
                    for sst in lvl:                       # >=L1 non-overlapping -> at most one covers
                        if sst.covers(k):
                            if k in sst.data: touched += 1
                            else: touched += (self.bloom_fp if use_bloom else 1)
                            break
        return touched / len(sample)


def run(strategy, n_puts=400_000, keyspace=50_000):
    lsm = LSM(strategy=strategy)
    for _ in range(n_puts):
        lsm.put(random.randint(1, keyspace))     # heavy overwrite -> obsolete versions
    lsm._flush()
    live = lsm.live_keys()
    existing = random.sample(list(live.keys()), 1000)
    missing = [random.randint(keyspace+1, keyspace*2) for _ in range(1000)]
    return {
        "strategy": strategy,
        "n_levels": len(lsm.levels),
        "files": [len(l) for l in lsm.levels],
        "ingested_MB": lsm.bytes_ingested/1e6,
        "written_MB": lsm.bytes_written/1e6,
        "write_amp": lsm.write_amp(),
        "space_amp": lsm.space_amp(),
        "read_amp_nobloom": lsm.read_amp(existing+missing, use_bloom=False),
        "read_amp_bloom": lsm.read_amp(existing+missing, use_bloom=True),
    }

print(f"Workload: 400,000 puts over 50,000 keys (8x overwrite), value=100B, "
      f"memtable=2000, fanout=10, L0_trigger=4\n")
hdr = ["strategy","n_levels","files/level","ingest MB","written MB","WRITE-amp","SPACE-amp","READ-amp(no bloom)","READ-amp(bloom 1%)"]
print("  " + " | ".join(hdr))
print("  " + "-+-".join("-"*len(h) for h in hdr))
for strat in ("leveled", "tiered"):
    r = run(strat)
    row = [r["strategy"], str(r["n_levels"]), str(r["files"]),
           f'{r["ingested_MB"]:.1f}', f'{r["written_MB"]:.1f}',
           f'{r["write_amp"]:.2f}x', f'{r["space_amp"]:.2f}x',
           f'{r["read_amp_nobloom"]:.2f}', f'{r["read_amp_bloom"]:.2f}']
    print("  " + " | ".join(v.ljust(len(hdr[i])) for i, v in enumerate(row)))
print("\nDONE.")
