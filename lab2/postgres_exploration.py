"""
Lab 2: PostgreSQL Exploration
Student: Talin Daga (24bcs10321)
Run: python3 postgres_exploration.py

Prerequisites:
    pip install psycopg2-binary
    PostgreSQL server must be running.

Edit DB_PARAMS below to match your setup.
"""

import time
import subprocess
import sys

try:
    import psycopg2
    from psycopg2 import sql as pgsql
except ImportError:
    print("ERROR: psycopg2 not found. Install it with:")
    print("       pip install psycopg2-binary")
    sys.exit(1)

# ── Edit these to match your PostgreSQL setup ────────────────────
DB_PARAMS = {
    "dbname":   "lab2_db",
    "user":     "postgres",
    "password": "postgres",
    "host":     "localhost",
    "port":     "5432",
}
TABLE = "employees"
# ─────────────────────────────────────────────────────────────────


def sep(title):
    print(f"\n{'='*62}")
    print(f"  {title}")
    print('='*62)


def connect(dbname=None):
    params = dict(DB_PARAMS)
    if dbname:
        params["dbname"] = dbname
    try:
        return psycopg2.connect(**params)
    except psycopg2.OperationalError as e:
        print(f"\n  CONNECTION ERROR: {e}")
        print("  Check that PostgreSQL is running and DB_PARAMS is correct.")
        sys.exit(1)


# ── Setup: create database + table + sample data ─────────────────
def setup():
    sep("SETUP: Database, Table, Sample Data")

    # Create DB if it doesn't exist (connect to 'postgres' first)
    conn0 = connect(dbname="postgres")
    conn0.autocommit = True
    cur0 = conn0.cursor()
    cur0.execute("SELECT 1 FROM pg_database WHERE datname = %s", (DB_PARAMS["dbname"],))
    if not cur0.fetchone():
        cur0.execute(pgsql.SQL("CREATE DATABASE {}").format(
            pgsql.Identifier(DB_PARAMS["dbname"])))
        print(f"\n  Created database '{DB_PARAMS['dbname']}'")
    else:
        print(f"\n  Database '{DB_PARAMS['dbname']}' already exists")
    conn0.close()

    conn = connect()
    cur  = conn.cursor()
    cur.execute(f"""
        CREATE TABLE IF NOT EXISTS {TABLE} (
            id         SERIAL PRIMARY KEY,
            name       VARCHAR(100) NOT NULL,
            department VARCHAR(50),
            salary     NUMERIC(10,2),
            email      VARCHAR(100)
        )
    """)
    conn.commit()

    cur.execute(f"SELECT COUNT(*) FROM {TABLE}")
    count = cur.fetchone()[0]
    target = 5000
    if count < target:
        to_insert = target - count
        print(f"  Inserting {to_insert} rows into '{TABLE}'...")
        data = [
            (f"Employee_{i}", f"Dept_{i % 10}", 50000 + (i % 50000), f"emp{i}@corp.com")
            for i in range(count, target)
        ]
        cur.executemany(
            f"INSERT INTO {TABLE} (name, department, salary, email) VALUES (%s,%s,%s,%s)",
            data
        )
        conn.commit()
        print("  Done.")
    else:
        print(f"  Table already has {count} rows — skipping insert.")

    conn.close()


# ── Section 1: Storage Information ───────────────────────────────
def storage_information():
    sep("1. DATABASE STORAGE INFORMATION")
    conn = connect()
    cur  = conn.cursor()

    # Block size
    cur.execute("SHOW block_size")
    block_size = int(cur.fetchone()[0])
    print(f"\n  block_size (PostgreSQL page size)   = {block_size} bytes")

    # Relation sizes
    cur.execute(f"SELECT pg_relation_size('{TABLE}')")
    rel_bytes = cur.fetchone()[0]
    cur.execute(f"SELECT pg_size_pretty(pg_relation_size('{TABLE}'))")
    rel_pretty = cur.fetchone()[0]
    print(f"  pg_relation_size('{TABLE}')        = {rel_bytes} bytes  ({rel_pretty})")

    cur.execute(f"SELECT pg_size_pretty(pg_total_relation_size('{TABLE}'))")
    total_pretty = cur.fetchone()[0]
    print(f"  pg_total_relation_size (with TOAST/indexes) = {total_pretty}")

    cur.execute(f"SELECT pg_size_pretty(pg_database_size('{DB_PARAMS['dbname']}'))")
    db_size = cur.fetchone()[0]
    print(f"  pg_database_size('{DB_PARAMS['dbname']}')        = {db_size}")

    # Page count from pg_class
    cur.execute(f"SELECT relpages, reltuples::bigint FROM pg_class WHERE relname = %s", (TABLE,))
    row = cur.fetchone()
    relpages, reltuples = row
    print(f"\n  relpages  (estimated page count)    = {relpages}")
    print(f"  reltuples (estimated row count)     = {reltuples}")

    calc = block_size * relpages
    print(f"\n  block_size × relpages = {block_size} × {relpages} = {calc} bytes")
    print(f"  pg_relation_size      = {rel_bytes} bytes")
    print(f"  Match: {'YES' if calc == rel_bytes else 'NO  (run VACUUM ANALYZE to refresh stats)'}")

    conn.close()


# ── Section 2: Query Performance ─────────────────────────────────
def query_performance():
    sep("2. QUERY PERFORMANCE MEASUREMENT")
    conn = connect()
    cur  = conn.cursor()

    query = (f"SELECT department, COUNT(*), AVG(salary), MAX(salary) "
             f"FROM {TABLE} GROUP BY department")
    N = 7

    print(f"\n  Query : {query}")
    print(f"  Runs  : {N}\n")

    times = []
    for _ in range(N):
        t0 = time.perf_counter()
        cur.execute(query); cur.fetchall()
        times.append((time.perf_counter() - t0) * 1000)

    avg = sum(times) / N
    print(f"  Python-side times (ms): {[f'{t:.3f}' for t in times]}")
    print(f"  Average               : {avg:.3f} ms")

    # EXPLAIN ANALYZE
    print(f"\n  EXPLAIN ANALYZE:")
    cur.execute(f"EXPLAIN ANALYZE {query}")
    for (line,) in cur.fetchall():
        print(f"    {line}")

    conn.close()


# ── Section 3: Process Monitoring ────────────────────────────────
def process_monitoring():
    sep("3. PROCESS MONITORING")

    print("\n  PostgreSQL server processes  (ps aux | grep postgres):\n")
    r = subprocess.run("ps aux | grep postgres | grep -v grep",
                       shell=True, capture_output=True, text=True)
    lines = r.stdout.strip().split('\n')
    if lines and lines[0]:
        print(f"  {'USER':<12}{'PID':<8}{'%CPU':<7}{'%MEM':<7}COMMAND")
        print(f"  {'-'*60}")
        for line in lines:
            p = line.split()
            if len(p) >= 11:
                user, pid, cpu, mem = p[0], p[1], p[2], p[3]
                cmd = ' '.join(p[10:])[:45]
                print(f"  {user:<12}{pid:<8}{cpu:<7}{mem:<7}{cmd}")
    else:
        print("  (no postgres processes found — is the server running?)")

    # pg_stat_activity
    print("\n  Active sessions (pg_stat_activity):\n")
    conn = connect()
    cur  = conn.cursor()
    cur.execute("""
        SELECT pid,
               usename,
               application_name,
               state,
               COALESCE(left(query, 55), '') AS short_query
        FROM   pg_stat_activity
        WHERE  datname = %s
    """, (DB_PARAMS["dbname"],))
    rows = cur.fetchall()
    print(f"  {'PID':<8}{'USER':<12}{'APP':<18}{'STATE':<12}QUERY")
    print(f"  {'-'*70}")
    for pid, user, app, state, q in rows:
        print(f"  {str(pid):<8}{str(user):<12}{str(app):<18}{str(state):<12}{q}")
    conn.close()

    print("\n  Key insight: PostgreSQL runs a postmaster + worker processes.")
    print("  Each client connection gets a dedicated backend process.")


# ── Main ──────────────────────────────────────────────────────────
def main():
    print("=" * 62)
    print("  PostgreSQL Exploration  —  Lab 2")
    print("  Student: Talin Daga (24bcs10321)")
    print("=" * 62)
    print(f"\n  Connecting to {DB_PARAMS['host']}:{DB_PARAMS['port']}"
          f"  db={DB_PARAMS['dbname']}  user={DB_PARAMS['user']}")

    setup()
    storage_information()
    query_performance()
    process_monitoring()

    print(f"\n{'='*62}")
    print("  PostgreSQL Exploration Complete")
    print(f"{'='*62}\n")


if __name__ == "__main__":
    main()
