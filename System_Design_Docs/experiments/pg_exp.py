"""Real PostgreSQL internals experiments via a bundled server (pgserver) + pg8000.
Covers: page/relation layout, EXPLAIN (ANALYZE, BUFFERS) multi-join, index plans,
MVCC (xmin/xmax/ctid), pageinspect heap tuples, dead-tuple bloat + VACUUM,
pg_stats planner statistics, and WAL volume. All output is REAL."""
import os, urllib.parse, pgserver, pg8000

HERE = os.path.dirname(os.path.abspath(__file__))
srv = pgserver.get_server(os.path.join(HERE, "pgdata"))
u = urllib.parse.urlparse(srv.get_uri())
con = pg8000.connect(user=u.username, password=u.password or "", host=u.hostname,
                     port=u.port, database=(u.path.lstrip("/") or "postgres"))
con.autocommit = True
cur = con.cursor()

def banner(t): print("\n" + "="*72 + f"\n{t}\n" + "="*72)
def run(sql): cur.execute(sql); return cur.fetchall()
def explain(sql):
    cur.execute(sql)
    print("\n".join(r[0] for r in cur.fetchall()))
def table(sql, headers):
    rows = run(sql)
    w = [max(len(str(h)), *(len(str(r[i])) for r in rows)) if rows else len(str(h)) for i, h in enumerate(headers)]
    print("  " + " | ".join(str(h).ljust(w[i]) for i, h in enumerate(headers)))
    print("  " + "-+-".join("-"*w[i] for i in range(len(headers))))
    for r in rows:
        print("  " + " | ".join(str(v).ljust(w[i]) for i, v in enumerate(r)))

print("VERSION:", run("SELECT version()")[0][0][:70])
print("block_size:", run("SHOW block_size")[0][0])

# ---------- setup ----------
cur.execute("SELECT setseed(0.42)")
for t in ("order_items", "orders", "products", "customers", "acct", "wtest"):
    cur.execute(f"DROP TABLE IF EXISTS {t} CASCADE")
for stmt in [
 "CREATE TABLE customers(id serial PRIMARY KEY, name text, city text)",
 "CREATE TABLE products(id serial PRIMARY KEY, name text, price numeric, category text)",
 "CREATE TABLE orders(id serial PRIMARY KEY, customer_id int, order_date date, total numeric)",
 "CREATE TABLE order_items(id serial PRIMARY KEY, order_id int, product_id int, qty int, price numeric)",
 "INSERT INTO customers(name,city) SELECT 'cust'||g,(ARRAY['Pune','Mumbai','Delhi','Bengaluru','Chennai','Kolkata','Hyderabad','Jaipur'])[1+floor(random()*8)] FROM generate_series(1,5000) g",
 "INSERT INTO products(name,price,category) SELECT 'prod'||g, round((random()*495+5)::numeric,2),(ARRAY['books','electronics','grocery','toys','apparel'])[1+floor(random()*5)] FROM generate_series(1,1000) g",
 "INSERT INTO orders(customer_id,order_date,total) SELECT 1+floor(random()*5000)::int, date '2026-01-01'+(floor(random()*180))::int, round((random()*1980+20)::numeric,2) FROM generate_series(1,50000) g",
 "INSERT INTO order_items(order_id,product_id,qty,price) SELECT 1+floor(random()*50000)::int,1+floor(random()*1000)::int,1+floor(random()*5)::int, round((random()*495+5)::numeric,2) FROM generate_series(1,150000) g",
 "ANALYZE",
]:
    cur.execute(stmt)
print("setup done: customers=5000 products=1000 orders=50000 order_items=150000")

banner("1. RELATION / PAGE LAYOUT (pg_class, block_size=8192)")
table("SELECT relname, relpages, reltuples::bigint, pg_size_pretty(pg_relation_size(oid)) "
      "FROM pg_class WHERE relname IN ('customers','products','orders','order_items') ORDER BY relpages DESC",
      ["relname","relpages","reltuples","size"])

banner("2. EXPLAIN (ANALYZE, BUFFERS): 4-table join + GROUP BY")
explain("""EXPLAIN (ANALYZE, BUFFERS, COSTS)
SELECT c.city, p.category, SUM(oi.qty*oi.price) AS rev
FROM customers c
JOIN orders o ON o.customer_id=c.id
JOIN order_items oi ON oi.order_id=o.id
JOIN products p ON p.id=oi.product_id
WHERE c.city='Pune'
GROUP BY c.city, p.category
ORDER BY rev DESC""")

banner("3a. Plan BEFORE index on orders.customer_id (selective lookup)")
explain("EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM orders WHERE customer_id=99")
banner("3b. Same query AFTER CREATE INDEX  (seq scan -> index scan)")
cur.execute("CREATE INDEX idx_orders_cust ON orders(customer_id)")
cur.execute("ANALYZE orders")
explain("EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM orders WHERE customer_id=99")

banner("4. MVCC: UPDATE creates a NEW tuple version (xmin/xmax/ctid)")
cur.execute("CREATE TABLE acct(id int PRIMARY KEY, owner text, bal int)")
cur.execute("INSERT INTO acct VALUES (1,'alice',100)")
print("After INSERT:")
table("SELECT ctid, xmin, xmax, bal FROM acct", ["ctid","xmin","xmax","bal"])
for i in range(3):
    cur.execute("UPDATE acct SET bal=bal-10 WHERE id=1")
    print(f"After UPDATE #{i+1}:")
    table("SELECT ctid, xmin, xmax, bal FROM acct", ["ctid","xmin","xmax","bal"])

banner("5. pageinspect: physical heap tuples (live + dead versions on the page)")
try:
    cur.execute("CREATE EXTENSION IF NOT EXISTS pageinspect")
    table("SELECT lp, lp_flags, t_xmin, t_xmax, t_ctid FROM heap_page_items(get_raw_page('acct',0)) ORDER BY lp",
          ["lp","lp_flags","t_xmin","t_xmax","t_ctid"])
    print("  (lp_flags=1 NORMAL, 0=UNUSED, 2=REDIRECT; t_xmax!=0 => superseded/dead version)")
except Exception as e:
    print("  pageinspect unavailable:", str(e)[:80])

banner("6. BLOAT + VACUUM: dead tuples from 500 updates, then reclaim")
for _ in range(500):
    cur.execute("UPDATE acct SET bal=bal+1 WHERE id=1")
def relsize(): return run("SELECT pg_size_pretty(pg_relation_size('acct'))")[0][0]
try:
    cur.execute("CREATE EXTENSION IF NOT EXISTS pgstattuple")
    r = run("SELECT tuple_count, dead_tuple_count, round(dead_tuple_percent::numeric,1) FROM pgstattuple('acct')")[0]
    print(f"  BEFORE VACUUM: live={r[0]}  dead={r[1]}  dead%={r[2]}  size={relsize()}")
    cur.execute("VACUUM acct")
    r = run("SELECT tuple_count, dead_tuple_count, round(dead_tuple_percent::numeric,1) FROM pgstattuple('acct')")[0]
    print(f"  AFTER  VACUUM: live={r[0]}  dead={r[1]}  dead%={r[2]}  size={relsize()}  (space kept for reuse)")
except Exception as e:
    print("  pgstattuple unavailable:", str(e)[:80])

banner("7. pg_stats: statistics the PLANNER uses (why it picks plans)")
table("SELECT attname, n_distinct, round(correlation::numeric,2) AS corr, "
      "array_length(most_common_vals,1) AS n_mcv "
      "FROM pg_stats WHERE tablename='orders' ORDER BY attname",
      ["attname","n_distinct","corr","n_mcv"])
print("  most_common city values (from customers):")
print("   ", run("SELECT most_common_vals FROM pg_stats WHERE tablename='customers' AND attname='city'")[0][0])

banner("8. WAL volume generated by a bulk write")
before = run("SELECT pg_current_wal_lsn()")[0][0]
cur.execute("CREATE TABLE wtest AS SELECT * FROM orders")  # ~50k rows
after = run("SELECT pg_current_wal_lsn()")[0][0]
diff = run(f"SELECT pg_size_pretty(pg_wal_lsn_diff('{after}','{before}')::bigint)")[0][0]
wfile = run("SELECT pg_walfile_name(pg_current_wal_lsn())")[0][0]
print(f"  WAL LSN before : {before}")
print(f"  WAL LSN after  : {after}")
print(f"  WAL generated  : {diff} for 'CREATE TABLE wtest AS SELECT * FROM orders' (50k rows)")
print(f"  current WAL seg: {wfile}")

con.close()
print("\nDONE.")
