"""
Lab 2: SQLite3 Exploration
Student: Talin Daga (24bcs10321)
Run: python3 sqlite3_exploration.py
"""

import sqlite3
import os
import time
import subprocess

DB_FILE = "test_lab2.db"


def sep(title):
    print(f"\n{'='*62}")
    print(f"  {title}")
    print('='*62)


# ── Section 1: File Size Analysis ────────────────────────────────
def file_size_analysis(conn):
    sep("1. FILE SIZE ANALYSIS")
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS employees (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            name      TEXT    NOT NULL,
            department TEXT,
            salary    REAL,
            email     TEXT
        )
    """)
    conn.commit()

    print(f"\n  {'Batch':<8} {'Total rows':<14} {'File size (bytes)':<22} {'File size (KB)'}")
    print(f"  {'-'*58}")

    batch_size = 1000
    for batch in range(5):
        start = batch * batch_size
        data = [
            (f"Employee_{i}", f"Dept_{i % 10}", 50000 + (i % 50000), f"emp{i}@corp.com")
            for i in range(start, start + batch_size)
        ]
        cur.executemany(
            "INSERT INTO employees (name, department, salary, email) VALUES (?,?,?,?)",
            data
        )
        conn.commit()
        sz = os.path.getsize(DB_FILE)
        rows = (batch + 1) * batch_size
        print(f"  {batch+1:<8} {rows:<14} {sz:<22} {sz/1024:.2f}")


# ── Section 2: Page Information ──────────────────────────────────
def page_information(conn):
    sep("2. PAGE INFORMATION")
    cur = conn.cursor()

    pragmas = [
        ("page_size",      "Bytes per page (default 4096)"),
        ("page_count",     "Total pages in the database file"),
        ("freelist_count", "Unused (free) pages"),
        ("cache_size",     "Pages held in the page cache"),
        ("journal_mode",   "Write-ahead or rollback journal mode"),
        ("synchronous",    "Durability level (0=OFF 1=NORMAL 2=FULL)"),
    ]
    print()
    for pragma, desc in pragmas:
        cur.execute(f"PRAGMA {pragma}")
        val = cur.fetchone()[0]
        print(f"  {pragma:<20} = {str(val):<12}  # {desc}")

    cur.execute("PRAGMA page_size");  ps = cur.fetchone()[0]
    cur.execute("PRAGMA page_count"); pc = cur.fetchone()[0]
    calc = ps * pc
    actual = os.path.getsize(DB_FILE)
    print(f"\n  page_size × page_count = {ps} × {pc} = {calc} bytes")
    print(f"  Actual file size       = {actual} bytes")
    print(f"  Match: {'YES' if calc == actual else 'NO  (diff = ' + str(abs(calc-actual)) + ' bytes)'}")


# ── Section 3: Memory-Mapped I/O ─────────────────────────────────
def mmap_investigation(conn):
    sep("3. MEMORY-MAPPED I/O (mmap)")
    cur = conn.cursor()

    cur.execute("PRAGMA mmap_size")
    default_val = cur.fetchone()[0]
    print(f"\n  Default mmap_size = {default_val} bytes  "
          f"({'disabled' if default_val == 0 else f'{default_val/(1024*1024):.0f} MB'})")

    cur.execute("PRAGMA mmap_size = 0")
    cur.execute("PRAGMA mmap_size")
    print(f"\n  After PRAGMA mmap_size = 0        -> {cur.fetchone()[0]} bytes  (mmap OFF)")

    cur.execute("PRAGMA mmap_size = 268435456")   # 256 MB
    cur.execute("PRAGMA mmap_size")
    enabled = cur.fetchone()[0]
    print(f"  After PRAGMA mmap_size = 268435456 -> {enabled} bytes  "
          f"({enabled/(1024*1024):.0f} MB, mmap ON)")

    print("\n  When mmap is OFF  : SQLite uses read()/write() system calls for each page.")
    print("  When mmap is ON   : SQLite maps the file into virtual address space;")
    print("                      the OS page cache handles reads — fewer system calls.")


# ── Section 4: Query Performance ─────────────────────────────────
def query_performance(conn):
    sep("4. QUERY PERFORMANCE MEASUREMENT")
    cur = conn.cursor()

    query = ("SELECT department, COUNT(*), AVG(salary), MAX(salary) "
             "FROM employees GROUP BY department")
    N = 7

    print(f"\n  Query : {query}")
    print(f"  Runs  : {N}\n")

    # mmap OFF
    cur.execute("PRAGMA mmap_size = 0")
    t_off = []
    for _ in range(N):
        t0 = time.perf_counter()
        cur.execute(query); cur.fetchall()
        t_off.append((time.perf_counter() - t0) * 1000)

    avg_off = sum(t_off) / N
    print(f"  mmap OFF  times (ms): {[f'{t:.3f}' for t in t_off]}")
    print(f"            average   : {avg_off:.3f} ms")

    # mmap ON (256 MB)
    cur.execute("PRAGMA mmap_size = 268435456")
    t_on = []
    for _ in range(N):
        t0 = time.perf_counter()
        cur.execute(query); cur.fetchall()
        t_on.append((time.perf_counter() - t0) * 1000)

    avg_on = sum(t_on) / N
    print(f"\n  mmap ON   times (ms): {[f'{t:.3f}' for t in t_on]}")
    print(f"            average   : {avg_on:.3f} ms")

    diff = avg_off - avg_on
    winner = "mmap ON" if diff > 0 else "mmap OFF"
    print(f"\n  Difference: {abs(diff):.3f} ms  =>  {winner} was faster")


# ── Section 5: Process Monitoring ────────────────────────────────
def process_monitoring():
    sep("5. PROCESS MONITORING")

    print("\n  Processes containing 'sqlite' in their name:")
    r = subprocess.run("ps aux | grep -i sqlite | grep -v grep",
                       shell=True, capture_output=True, text=True)
    out = r.stdout.strip()
    print(out if out else "  (none — SQLite is in-process, not a server daemon)")

    print("\n  This Python process resource usage:")
    pid = os.getpid()
    r2 = subprocess.run(f"ps -o pid,rss,vsz,%cpu,comm -p {pid}",
                        shell=True, capture_output=True, text=True)
    for line in r2.stdout.strip().split('\n'):
        print(f"    {line}")

    print("\n  Key insight: SQLite runs inside the application process.")
    print("  There is no separate server — the OS manages the file directly.")


# ── Main ──────────────────────────────────────────────────────────
def main():
    print("=" * 62)
    print("  SQLite3 Exploration  —  Lab 2")
    print("  Student: Talin Daga (24bcs10321)")
    print("=" * 62)

    if os.path.exists(DB_FILE):
        os.remove(DB_FILE)
        print(f"\n  Removed old '{DB_FILE}'")

    conn = sqlite3.connect(DB_FILE)
    try:
        file_size_analysis(conn)
        page_information(conn)
        mmap_investigation(conn)
        query_performance(conn)
        process_monitoring()
    finally:
        conn.close()

    print(f"\n{'='*62}")
    print("  SQLite3 Exploration Complete")
    print(f"{'='*62}\n")


if __name__ == "__main__":
    main()
