import re, glob
for f in glob.glob("*.out"):
    print(f"=== {f} ===")
    with open(f) as file:
        text = file.read()
        wamp = re.search(r"Sum\s+\d+/\d+.*?\s+([\d\.]+)\s+[\d\.]+\s+[\d\.]+\s+[\d\.]+\s+", text)
        if wamp: print("W-Amp:", wamp.group(1))
        dbsz = re.search(r"([0-9\.]+G)\s+/tmp/rocksdb_test", text)
        if dbsz: print("DB Size:", dbsz.group(1))
        useful = re.search(r"rocksdb\.bloom\.filter\.useful COUNT : (\d+)", text)
        if useful: print("Bloom Useful:", useful.group(1))
        fpos = re.search(r"rocksdb\.bloom\.filter\.full\.positive COUNT : (\d+)", text)
        if fpos: print("Bloom Full Positive:", fpos.group(1))
        p50 = re.search(r"rocksdb\.db\.get\.micros P50 : ([\d\.]+)", text)
        p99 = re.search(r"rocksdb\.db\.get\.micros.*P99 : ([\d\.]+)", text)
        if p50 and p99: print(f"Get Latency: p50={p50.group(1)}, p99={p99.group(1)}")
