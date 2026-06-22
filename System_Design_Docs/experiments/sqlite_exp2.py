"""Follow-up SQLite experiments: clustered storage and WAL behavior."""
import os, sqlite3

HERE = os.path.dirname(__file__)
def fresh(name):
    p = os.path.join(HERE, name)
    for ext in ("", "-wal", "-shm", "-journal"):
        try: os.remove(p+ext)
        except FileNotFoundError: pass
    return p

print("="*68, "\n8. WITHOUT ROWID (clustered) vs rowid+secondary index (file size)\n", "="*68)
data = [(f"key{i:06d}", "x"*40) for i in range(20000)]

a = fresh("kv_rowid.db"); con = sqlite3.connect(a); c = con.cursor()
c.execute("CREATE TABLE kv(k TEXT, v TEXT)")
c.executemany("INSERT INTO kv VALUES(?,?)", data)
c.execute("CREATE INDEX idx_kv ON kv(k)")          # secondary index to look up by k
con.commit(); pc_a = c.execute("PRAGMA page_count").fetchone()[0]; con.close()

b = fresh("kv_clustered.db"); con = sqlite3.connect(b); c = con.cursor()
c.execute("CREATE TABLE kv(k TEXT PRIMARY KEY, v TEXT) WITHOUT ROWID")
c.executemany("INSERT INTO kv VALUES(?,?)", data)
con.commit(); pc_b = c.execute("PRAGMA page_count").fetchone()[0]; con.close()

print(f"  rowid table + secondary index : {pc_a:4d} pages  {os.path.getsize(a):>9,} bytes")
print(f"  WITHOUT ROWID (clustered PK)  : {pc_b:4d} pages  {os.path.getsize(b):>9,} bytes")
print(f"  -> clustered avoids storing keys twice; lookups by k hit the table B-tree directly")

print("\n" + "="*68, "\n9. WAL: committed changes accumulate in -wal until checkpoint\n", "="*68)
p = fresh("wal_demo.db"); con = sqlite3.connect(p, isolation_level=None); c = con.cursor()
c.execute("PRAGMA journal_mode=WAL"); c.execute("PRAGMA wal_autocheckpoint=0")
c.execute("CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)")
def walsize(): return os.path.getsize(p+"-wal") if os.path.exists(p+"-wal") else 0
print(f"  after CREATE TABLE        : main={os.path.getsize(p):>7,}  wal={walsize():>7,} bytes")
c.execute("BEGIN");
for i in range(5000): c.execute("INSERT INTO t VALUES(?,?)", (i, "payload"*8))
c.execute("COMMIT")
print(f"  after 5000 commits        : main={os.path.getsize(p):>7,}  wal={walsize():>7,} bytes  <- durable in WAL, not yet in main")
c.execute("PRAGMA wal_checkpoint(TRUNCATE)")
print(f"  after wal_checkpoint       : main={os.path.getsize(p):>7,}  wal={walsize():>7,} bytes  <- pages folded into main db")
con.close()
print("\nDONE.")
