"""SQLite internals experiments for the Advanced DBMS assignment.
Builds a small e-commerce dataset and probes page layout, query planning,
indexing, the WITHOUT ROWID (clustered) option, and the WAL journal mode.
All numbers printed here are REAL and reproducible (seeded RNG)."""
import os, sqlite3, time, random, textwrap

random.seed(42)
DB = os.path.join(os.path.dirname(__file__), "shop.db")
for ext in ("", "-wal", "-shm", "-journal"):
    try: os.remove(DB + ext)
    except FileNotFoundError: pass

con = sqlite3.connect(DB)
cur = con.cursor()
cur.executescript("""
CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT, city TEXT);
CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT, price REAL, category TEXT);
CREATE TABLE orders   (id INTEGER PRIMARY KEY, customer_id INTEGER, order_date TEXT, total REAL);
CREATE TABLE order_items(id INTEGER PRIMARY KEY, order_id INTEGER, product_id INTEGER, qty INTEGER, price REAL);
""")

cities = ["Pune","Mumbai","Delhi","Bengaluru","Chennai","Kolkata","Hyderabad","Jaipur"]
cats   = ["books","electronics","grocery","toys","apparel"]
cur.executemany("INSERT INTO customers VALUES(?,?,?)",
    [(i, f"cust{i}", random.choice(cities)) for i in range(1, 5001)])
cur.executemany("INSERT INTO products VALUES(?,?,?,?)",
    [(i, f"prod{i}", round(random.uniform(5, 500), 2), random.choice(cats)) for i in range(1, 1001)])
orders = [(i, random.randint(1, 5000), f"2026-0{random.randint(1,6)}-15", round(random.uniform(20, 2000), 2))
          for i in range(1, 50001)]
cur.executemany("INSERT INTO orders VALUES(?,?,?,?)", orders)
items, k = [], 1
for oid in range(1, 50001):
    for _ in range(random.randint(1, 4)):
        items.append((k, oid, random.randint(1, 1000), random.randint(1, 5), round(random.uniform(5, 500), 2)))
        k += 1
cur.executemany("INSERT INTO order_items VALUES(?,?,?,?,?)", items)
con.commit()
print(f"rows: customers=5000 products=1000 orders=50000 order_items={len(items)}")

def banner(t): print("\n" + "="*68 + f"\n{t}\n" + "="*68)

banner("1. PAGE LAYOUT (PRAGMA)")
for p in ("page_size", "page_count", "freelist_count", "cache_size", "journal_mode"):
    print(f"  {p:15s} = {cur.execute('PRAGMA '+p).fetchone()[0]}")
print(f"  file size      = {os.path.getsize(DB):,} bytes "
      f"(= page_size * page_count = {cur.execute('PRAGMA page_size').fetchone()[0]*cur.execute('PRAGMA page_count').fetchone()[0]:,})")

banner("2. QUERY PLAN: primary-key point lookup (rowid B-tree)")
for r in cur.execute("EXPLAIN QUERY PLAN SELECT * FROM orders WHERE id = 42"): print("  ", r[-1])

banner("3. QUERY PLAN: filter on UN-indexed column -> full scan")
for r in cur.execute("EXPLAIN QUERY PLAN SELECT * FROM orders WHERE customer_id = 99"): print("  ", r[-1])

banner("4. Create index on orders(customer_id), re-plan -> index search")
cur.execute("CREATE INDEX idx_orders_cust ON orders(customer_id)")
for r in cur.execute("EXPLAIN QUERY PLAN SELECT * FROM orders WHERE customer_id = 99"): print("  ", r[-1])

banner("5. TIMING: same query, full scan vs index search (avg of 200 runs)")
con2 = sqlite3.connect(DB); c2 = con2.cursor()
c2.execute("DROP INDEX idx_orders_cust")
t0=time.perf_counter()
for _ in range(200): c2.execute("SELECT * FROM orders WHERE customer_id=99").fetchall()
us_noidx=(time.perf_counter()-t0)/200*1e6
c2.execute("CREATE INDEX idx_orders_cust ON orders(customer_id)")
t0=time.perf_counter()
for _ in range(200): c2.execute("SELECT * FROM orders WHERE customer_id=99").fetchall()
us_idx=(time.perf_counter()-t0)/200*1e6
print(f"  full table scan : {us_noidx:8.1f} us / query")
print(f"  index search    : {us_idx:8.1f} us / query")
print(f"  speedup         : {us_noidx/us_idx:6.1f}x")
con2.close()

banner("6. QUERY PLAN: 4-table join (customers x orders x order_items x products)")
join_sql = """
SELECT c.city, p.category, SUM(oi.qty*oi.price) rev
FROM customers c
JOIN orders o       ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id    = o.id
JOIN products p     ON p.id           = oi.product_id
WHERE c.city = 'Pune'
GROUP BY c.city, p.category"""
cur.execute("CREATE INDEX idx_items_order ON order_items(order_id)")
cur.execute("CREATE INDEX idx_cust_city  ON customers(city)")
for r in cur.execute("EXPLAIN QUERY PLAN " + join_sql): print("  ", r[-1])

banner("7. EXPLAIN (VDBE bytecode) for a simple query - first 14 opcodes")
rows = cur.execute("EXPLAIN SELECT total FROM orders WHERE id = 42").fetchall()
print(f"  {'addr':>4} {'opcode':14} {'p1':>4} {'p2':>4} {'p3':>4}  {'comment'}")
for r in rows[:14]:
    print(f"  {r[0]:>4} {r[1]:14} {r[2]:>4} {r[3]:>4} {r[4]:>4}  {r[7] or ''}")
print(f"  ... ({len(rows)} opcodes total)")

banner("8. WITHOUT ROWID (clustered) vs ordinary rowid table - storage")
cur.executescript("""
CREATE TABLE kv_rowid(k TEXT, v TEXT);
CREATE TABLE kv_clustered(k TEXT PRIMARY KEY, v TEXT) WITHOUT ROWID;
""")
data = [(f"key{i:06d}", "x"*40) for i in range(20000)]
cur.executemany("INSERT INTO kv_rowid VALUES(?,?)", data)
cur.execute("CREATE INDEX idx_kv ON kv_rowid(k)")
cur.executemany("INSERT INTO kv_clustered VALUES(?,?)", data)
con.commit()
def tbl_pages(name):
    return cur.execute("SELECT COUNT(*) FROM dbstat WHERE name=?", (name,)).fetchone()[0]
try:
    print(f"  kv_rowid table+index pages : {tbl_pages('kv_rowid')+tbl_pages('idx_kv')}")
    print(f"  kv_clustered (WITHOUT ROWID): {tbl_pages('kv_clustered')}  (data lives in the PK B-tree, no separate index)")
except sqlite3.OperationalError as e:
    print("  dbstat not available:", e)

banner("9. WAL MODE: switch journal mode, observe side files")
cur.execute("PRAGMA journal_mode = WAL")
cur.execute("INSERT INTO customers VALUES(99999,'walcust','Pune')")
# do NOT commit yet -> change sits in -wal file, readers still see old snapshot
print("  journal_mode now =", cur.execute("PRAGMA journal_mode").fetchone()[0])
for ext in ("-wal","-shm"):
    p = DB+ext
    print(f"  {os.path.basename(p):16} exists={os.path.exists(p)} size={os.path.getsize(p) if os.path.exists(p) else 0} bytes")
con.commit()
con.close()
print("\nDONE.")
