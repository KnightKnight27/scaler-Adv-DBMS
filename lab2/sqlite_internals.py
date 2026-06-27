#!/usr/bin/env python3
"""
Lab 2 — SQLite3 Internals Explorer

This script demonstrates SQLite3's internal architecture by:
1. Creating a database and inspecting PRAGMA settings
2. Analyzing page structure and B-Tree organization
3. Demonstrating WAL mode vs rollback journal
4. Showing mmap behavior
5. Performing schema introspection

Run: python3 sqlite_internals.py
"""

import sqlite3
import os
import struct
import sys
import time


# ──────────────────────────────────────────────
# Utility
# ──────────────────────────────────────────────
def banner(title):
    width = 60
    print(f"\n{'═' * width}")
    print(f"  {title}")
    print(f"{'═' * width}")


def section(title):
    print(f"\n--- {title} ---")


# ──────────────────────────────────────────────
# Section 1: PRAGMA Settings & Database Configuration
# ──────────────────────────────────────────────
def explore_pragmas(db_path):
    banner("Section 1: PRAGMA Settings & Configuration")

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    pragmas = {
        "page_size": "Size of each B-Tree page (default 4096)",
        "journal_mode": "How transactions are journaled (delete/wal/memory/off)",
        "cache_size": "Number of pages in the page cache (negative = KB)",
        "mmap_size": "Maximum mmap region size (0 = disabled)",
        "synchronous": "How aggressively SQLite syncs to disk (0=OFF,1=NORMAL,2=FULL)",
        "auto_vacuum": "Whether unused pages are reclaimed (0=NONE,1=FULL,2=INCREMENTAL)",
        "locking_mode": "Lock behavior (NORMAL or EXCLUSIVE)",
        "temp_store": "Where temp tables are stored (0=DEFAULT,1=FILE,2=MEMORY)",
        "wal_autocheckpoint": "Pages before auto-checkpoint in WAL mode",
        "freelist_count": "Number of unused pages on the freelist",
        "page_count": "Total pages in the database file",
        "max_page_count": "Maximum allowed pages",
        "encoding": "Text encoding (UTF-8/UTF-16le/UTF-16be)",
        "compile_options": "Compile-time options of the SQLite library",
    }

    print(f"\n{'PRAGMA':<25} {'Value':<20} Description")
    print(f"{'─' * 25} {'─' * 20} {'─' * 50}")

    for pragma, desc in pragmas.items():
        try:
            cur.execute(f"PRAGMA {pragma}")
            rows = cur.fetchall()
            if len(rows) == 1:
                val = str(rows[0][0])
            elif len(rows) > 1:
                val = f"[{len(rows)} items]"
            else:
                val = "(empty)"
            print(f"{pragma:<25} {val:<20} {desc}")
        except sqlite3.Error as e:
            print(f"{pragma:<25} {'ERROR':<20} {e}")

    # Show compile options separately
    section("SQLite Compile Options")
    cur.execute("PRAGMA compile_options")
    options = [row[0] for row in cur.fetchall()]
    for opt in options:
        print(f"  • {opt}")

    conn.close()


# ──────────────────────────────────────────────
# Section 2: Database File Header Analysis
# ──────────────────────────────────────────────
def analyze_db_header(db_path):
    banner("Section 2: Database File Header (first 100 bytes)")

    if not os.path.exists(db_path) or os.path.getsize(db_path) == 0:
        print("  Database file is empty or doesn't exist yet.")
        return

    with open(db_path, "rb") as f:
        header = f.read(100)

    if len(header) < 100:
        print("  Header too small — database may not be initialized.")
        return

    # SQLite database header format (first 100 bytes)
    # See: https://www.sqlite.org/fileformat2.html
    fields = [
        ("Magic string", header[0:16].decode("ascii", errors="replace").rstrip("\x00")),
        ("Page size", struct.unpack(">H", header[16:18])[0]),
        ("File format write version", header[18]),
        ("File format read version", header[19]),
        ("Reserved space per page", header[20]),
        ("Max embedded payload fraction", header[21]),
        ("Min embedded payload fraction", header[22]),
        ("Leaf payload fraction", header[23]),
        ("File change counter", struct.unpack(">I", header[24:28])[0]),
        ("Database size (pages)", struct.unpack(">I", header[28:32])[0]),
        ("First freelist trunk page", struct.unpack(">I", header[32:36])[0]),
        ("Total freelist pages", struct.unpack(">I", header[36:40])[0]),
        ("Schema cookie", struct.unpack(">I", header[40:44])[0]),
        ("Schema format number", struct.unpack(">I", header[44:48])[0]),
        ("Default page cache size", struct.unpack(">I", header[48:52])[0]),
        ("Text encoding", {1: "UTF-8", 2: "UTF-16le", 3: "UTF-16be"}.get(
            struct.unpack(">I", header[56:60])[0], "unknown"
        )),
        ("User version", struct.unpack(">I", header[60:64])[0]),
        ("Application ID", struct.unpack(">I", header[68:72])[0]),
        ("SQLite version number", struct.unpack(">I", header[96:100])[0]),
    ]

    # Handle page_size = 1 meaning 65536
    page_size = fields[1][1]
    if page_size == 1:
        page_size = 65536
        fields[1] = ("Page size", f"1 (= 65536)")

    print(f"\n  {'Field':<35} Value")
    print(f"  {'─' * 35} {'─' * 30}")
    for name, value in fields:
        print(f"  {name:<35} {value}")

    # Decode SQLite version number
    version_num = fields[-1][1]
    major = version_num // 1000000
    minor = (version_num % 1000000) // 1000
    patch = version_num % 1000
    print(f"\n  SQLite version: {major}.{minor}.{patch}")


# ──────────────────────────────────────────────
# Section 3: Create Tables & Analyze B-Tree Structure
# ──────────────────────────────────────────────
def explore_btree_structure(db_path):
    banner("Section 3: B-Tree Page Structure")

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    # Create sample tables
    cur.execute("DROP TABLE IF EXISTS students")
    cur.execute("DROP TABLE IF EXISTS courses")
    cur.execute("DROP TABLE IF EXISTS enrollments")

    cur.execute("""
        CREATE TABLE students (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            email TEXT UNIQUE,
            gpa REAL DEFAULT 0.0
        )
    """)

    cur.execute("""
        CREATE TABLE courses (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            credits INTEGER DEFAULT 3
        )
    """)

    cur.execute("""
        CREATE TABLE enrollments (
            student_id INTEGER REFERENCES students(id),
            course_id INTEGER REFERENCES courses(id),
            grade TEXT,
            PRIMARY KEY (student_id, course_id)
        )
    """)

    # Insert sample data
    students = [
        (1, "Alice", "alice@example.com", 3.8),
        (2, "Bob", "bob@example.com", 3.5),
        (3, "Charlie", "charlie@example.com", 3.9),
        (4, "Diana", "diana@example.com", 3.2),
        (5, "Eve", "eve@example.com", 3.7),
    ]
    cur.executemany("INSERT INTO students VALUES (?, ?, ?, ?)", students)

    courses = [
        (101, "Advanced DBMS", 4),
        (102, "Operating Systems", 3),
        (103, "Algorithms", 3),
    ]
    cur.executemany("INSERT INTO courses VALUES (?, ?, ?)", courses)

    enrollments = [
        (1, 101, "A"), (1, 102, "B+"), (2, 101, "A-"),
        (3, 103, "A"), (4, 101, "B"), (5, 102, "A"),
    ]
    cur.executemany("INSERT INTO enrollments VALUES (?, ?, ?)", enrollments)
    conn.commit()

    # Inspect schema
    section("Schema (sqlite_master)")
    cur.execute("SELECT type, name, tbl_name, rootpage, sql FROM sqlite_master ORDER BY rootpage")
    rows = cur.fetchall()
    print(f"\n  {'Type':<8} {'Name':<25} {'Table':<15} {'Root Page':<10} SQL")
    print(f"  {'─' * 8} {'─' * 25} {'─' * 15} {'─' * 10} {'─' * 40}")
    for row in rows:
        sql_short = (row[4][:37] + "...") if row[4] and len(row[4]) > 40 else (row[4] or "")
        print(f"  {row[0]:<8} {row[1]:<25} {row[2]:<15} {row[3]:<10} {sql_short}")

    print("\n  Key insight: Each table and index is a separate B-Tree.")
    print("  'rootpage' is the B-Tree root's page number in the database file.")

    # Page count and size
    section("Database Size Analysis")
    cur.execute("PRAGMA page_count")
    page_count = cur.fetchone()[0]
    cur.execute("PRAGMA page_size")
    page_size = cur.fetchone()[0]
    file_size = os.path.getsize(db_path)

    print(f"  Page size:     {page_size} bytes")
    print(f"  Page count:    {page_count}")
    print(f"  Calculated:    {page_count * page_size} bytes")
    print(f"  Actual file:   {file_size} bytes")
    print(f"  Overhead:      {file_size - page_count * page_size} bytes")

    conn.close()


# ──────────────────────────────────────────────
# Section 4: WAL Mode vs Rollback Journal
# ──────────────────────────────────────────────
def explore_journal_modes(db_path):
    banner("Section 4: WAL Mode vs Rollback Journal")

    # Default mode (rollback journal / DELETE)
    section("Rollback Journal Mode (default)")
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("PRAGMA journal_mode")
    mode = cur.fetchone()[0]
    print(f"  Current journal_mode: {mode}")
    print(f"  Behavior: SQLite creates a rollback journal file ({db_path}-journal)")
    print(f"  On COMMIT: journal is deleted (hence 'delete' mode)")
    print(f"  Pros: Simple, compatible with all platforms")
    print(f"  Cons: Writers block readers, readers block writers")
    conn.close()

    # Switch to WAL mode
    section("Write-Ahead Log (WAL) Mode")
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("PRAGMA journal_mode=WAL")
    mode = cur.fetchone()[0]
    print(f"  Switched to: {mode}")
    print(f"  WAL file: {db_path}-wal")
    print(f"  SHM file: {db_path}-shm (shared memory for WAL index)")
    print(f"  Behavior: Changes are APPENDED to the WAL file")
    print(f"  On COMMIT: WAL record is synced, main DB is unchanged")
    print(f"  Checkpoint: Periodically copies WAL changes back to main DB")
    print(f"  Pros: Readers don't block writers, writers don't block readers")
    print(f"  Cons: Slightly more complex, WAL file can grow large")

    # Check for WAL/SHM files
    wal_path = db_path + "-wal"
    shm_path = db_path + "-shm"
    print(f"\n  WAL file exists: {os.path.exists(wal_path)}")
    print(f"  SHM file exists: {os.path.exists(shm_path)}")

    # Demonstrate concurrent reads during write
    section("Concurrent Read/Write Demo in WAL Mode")
    # Start a write transaction
    cur.execute("BEGIN")
    cur.execute("INSERT INTO students VALUES (6, 'Frank', 'frank@example.com', 3.6)")

    # Open a second connection — can read even though write is in progress!
    conn2 = sqlite3.connect(db_path)
    cur2 = conn2.cursor()
    cur2.execute("SELECT COUNT(*) FROM students")
    count = cur2.fetchone()[0]
    print(f"  Writer has uncommitted INSERT, reader sees {count} rows (snapshot isolation!)")
    conn2.close()

    # Commit the write
    conn.commit()

    # Checkpoint info
    cur.execute("PRAGMA wal_checkpoint(PASSIVE)")
    result = cur.fetchone()
    print(f"  WAL checkpoint result: busy={result[0]}, log_pages={result[1]}, checkpointed={result[2]}")

    # Switch back to delete mode
    cur.execute("PRAGMA journal_mode=DELETE")
    conn.close()


# ──────────────────────────────────────────────
# Section 5: mmap Configuration
# ──────────────────────────────────────────────
def explore_mmap(db_path):
    banner("Section 5: Memory-Mapped I/O (mmap) in SQLite")

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    # Check current mmap setting
    cur.execute("PRAGMA mmap_size")
    current = cur.fetchone()[0]
    print(f"  Current mmap_size: {current} bytes")
    if current == 0:
        print("  mmap is DISABLED (default)")
    else:
        print(f"  mmap is ENABLED up to {current / (1024*1024):.1f} MB")

    # Enable mmap
    section("Enabling mmap")
    mmap_target = 256 * 1024 * 1024  # 256 MB
    cur.execute(f"PRAGMA mmap_size={mmap_target}")
    actual = cur.fetchone()[0]
    print(f"  Requested: {mmap_target} bytes ({mmap_target / (1024*1024):.0f} MB)")
    print(f"  Actual:    {actual} bytes ({actual / (1024*1024):.0f} MB)")

    print("""
  How mmap works in SQLite:
  ┌──────────────────────────────────────────────────────────────┐
  │  Without mmap (default):                                      │
  │    read() syscall → kernel copies data → userspace buffer     │
  │    (Two copies: disk→page_cache, page_cache→userspace)        │
  │                                                                │
  │  With mmap:                                                    │
  │    Process maps DB file into virtual memory                    │
  │    Access = pointer dereference → page fault → page loaded     │
  │    (One copy: disk→shared page, accessed directly)             │
  │                                                                │
  │  Trade-offs:                                                   │
  │    ✓ Faster reads (fewer copies, no syscall overhead)          │
  │    ✓ Kernel manages pages automatically                        │
  │    ✗ Signals (SIGBUS) on I/O errors instead of error codes     │
  │    ✗ Less control over memory usage                            │
  └──────────────────────────────────────────────────────────────┘
    """)

    # Reset mmap
    cur.execute("PRAGMA mmap_size=0")
    conn.close()


# ──────────────────────────────────────────────
# Section 6: Query Execution & EXPLAIN
# ──────────────────────────────────────────────
def explore_query_execution(db_path):
    banner("Section 6: Query Execution Plan (EXPLAIN)")

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    queries = [
        ("Simple SELECT", "SELECT * FROM students WHERE id = 3"),
        ("Full table scan", "SELECT * FROM students WHERE gpa > 3.5"),
        ("JOIN query", """
            SELECT s.name, c.title, e.grade
            FROM students s
            JOIN enrollments e ON s.id = e.student_id
            JOIN courses c ON e.course_id = c.id
            WHERE s.gpa > 3.5
        """),
        ("Aggregate", "SELECT AVG(gpa) as avg_gpa FROM students"),
        ("Subquery", "SELECT * FROM students WHERE id IN (SELECT student_id FROM enrollments WHERE grade = 'A')"),
    ]

    for title, query in queries:
        section(f"EXPLAIN QUERY PLAN: {title}")
        print(f"  SQL: {' '.join(query.split())}")
        cur.execute(f"EXPLAIN QUERY PLAN {query}")
        plan = cur.fetchall()
        for row in plan:
            indent = "    " * row[1] if row[1] else ""
            print(f"  {indent}id={row[0]} parent={row[1]} → {row[3]}")
        print()

    conn.close()


# ──────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────
def main():
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║     Lab 2: SQLite3 Internals Explorer                      ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print(f"  Python sqlite3 module version: {sqlite3.version}")
    print(f"  SQLite library version: {sqlite3.sqlite_version}")

    db_path = "lab2_test.db"

    # Clean start
    for ext in ["", "-wal", "-shm", "-journal"]:
        path = db_path + ext
        if os.path.exists(path):
            os.remove(path)

    explore_pragmas(db_path)
    analyze_db_header(db_path)
    explore_btree_structure(db_path)
    analyze_db_header(db_path)  # Re-analyze after data insertion
    explore_journal_modes(db_path)
    explore_mmap(db_path)
    explore_query_execution(db_path)

    # Cleanup
    for ext in ["", "-wal", "-shm", "-journal"]:
        path = db_path + ext
        if os.path.exists(path):
            os.remove(path)

    print("\n╔══════════════════════════════════════════════════════════════╗")
    print("║  Lab 2 Complete!                                            ║")
    print("╚══════════════════════════════════════════════════════════════╝")


if __name__ == "__main__":
    main()
