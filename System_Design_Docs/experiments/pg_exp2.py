"""Follow-up: canonical PostgreSQL bloat + VACUUM demo using CORE features only.
A full-table UPDATE rewrites every row (append-only MVCC), doubling the heap;
VACUUM frees the dead versions for REUSE so the next full update does not grow further."""
import os, urllib.parse, pgserver, pg8000
HERE = os.path.dirname(os.path.abspath(__file__))
srv = pgserver.get_server(os.path.join(HERE, "pgdata"))
u = urllib.parse.urlparse(srv.get_uri())
con = pg8000.connect(user=u.username, password=u.password or "", host=u.hostname, port=u.port,
                     database=(u.path.lstrip("/") or "postgres"))
con.autocommit = True
cur = con.cursor()
def run(s): cur.execute(s); return cur.fetchall()

print("Available extensions:", ", ".join(r[0] for r in run("SELECT name FROM pg_available_extensions ORDER BY name")))

print("\n" + "="*72)
print("BLOAT + VACUUM (full-table UPDATE, 100k rows, autovacuum OFF)")
print("="*72)
cur.execute("DROP TABLE IF EXISTS bloaty")
cur.execute("CREATE TABLE bloaty(id int PRIMARY KEY, v int) WITH (autovacuum_enabled=false)")
cur.execute("INSERT INTO bloaty SELECT g,0 FROM generate_series(1,100000) g")
def stat():
    s = run("SELECT pg_size_pretty(pg_relation_size('bloaty')), "
            "(SELECT n_live_tup FROM pg_stat_user_tables WHERE relname='bloaty'), "
            "(SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname='bloaty')")[0]
    return f"size={s[0]:>9}  live={s[1]}  dead={s[2]}"
print(f"  after load (100k rows)   : {stat()}")
cur.execute("UPDATE bloaty SET v=v+1")          # rewrites all 100k -> 100k dead versions
print(f"  after UPDATE all rows    : {stat()}   <- heap ~doubled, 100k dead versions")
cur.execute("VACUUM bloaty")
print(f"  after VACUUM             : {stat()}   <- dead reclaimed, file size kept as free space")
cur.execute("UPDATE bloaty SET v=v+1")          # reuses freed space
print(f"  after 2nd UPDATE all     : {stat()}   <- space REUSED, no further growth")
con.close()
print("\nDONE.")
