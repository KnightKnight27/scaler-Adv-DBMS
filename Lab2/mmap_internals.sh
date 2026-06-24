#!/bin/bash
# lab2_mmap_internals.sh
# Explores SQLite3 internals: mmap, page_size, PRAGMA commands
# and shows how SQLite uses memory-mapped I/O

DB="lab2_mmap.db"

echo "============================================"
echo "  Lab 2: SQLite3 Internals — mmap + PRAGMA"
echo "============================================"

# ── SECTION 1: CREATE DATABASE ───────────────────────────
echo ""
echo ">>> Section 1: Create database and populate"
echo ""

rm -f $DB

sqlite3 $DB <<EOF
CREATE TABLE employees (
    id      INTEGER PRIMARY KEY,
    name    TEXT    NOT NULL,
    dept    TEXT,
    salary  INTEGER
);

INSERT INTO employees VALUES (1, 'Alice',   'Engineering', 90000);
INSERT INTO employees VALUES (2, 'Bob',     'Marketing',   70000);
INSERT INTO employees VALUES (3, 'Charlie', 'Engineering', 95000);
INSERT INTO employees VALUES (4, 'Diana',   'HR',          65000);
INSERT INTO employees VALUES (5, 'Eve',     'Engineering', 88000);

SELECT * FROM employees;
EOF

echo ""
echo "Database file: $DB"
echo "File size: $(wc -c < $DB) bytes"

# ── SECTION 2: PRAGMA INSPECTION ─────────────────────────
echo ""
echo ">>> Section 2: PRAGMA — SQLite internal settings"
echo ""

sqlite3 $DB <<EOF
-- page size: unit of I/O between SQLite and OS (default 4096 bytes)
PRAGMA page_size;

-- total pages used by this database
PRAGMA page_count;

-- how many pages are free (not holding data)
PRAGMA freelist_count;

-- journal mode: how SQLite handles crash recovery
-- DELETE = classic rollback journal (default)
-- WAL    = write-ahead log (better concurrency)
PRAGMA journal_mode;

-- cache size: how many pages SQLite keeps in memory
-- negative value = size in KB, positive = number of pages
PRAGMA cache_size;

-- mmap_size: how many bytes SQLite will memory-map
-- 0 means mmap is disabled by default
PRAGMA mmap_size;

-- encoding used for text storage
PRAGMA encoding;

-- synchronous: how often SQLite calls fsync()
-- 0=OFF 1=NORMAL 2=FULL 3=EXTRA
PRAGMA synchronous;
EOF

# ── SECTION 3: ENABLE AND OBSERVE MMAP ───────────────────
echo ""
echo ">>> Section 3: Enabling mmap and observing effect"
echo ""

sqlite3 $DB <<EOF
-- enable mmap for up to 256MB
-- SQLite will use mmap() syscall instead of read() for file access
PRAGMA mmap_size = 268435456;

-- verify it was set
PRAGMA mmap_size;

-- now run a query — internally SQLite reads via mmap
SELECT dept, AVG(salary) as avg_salary
FROM employees
GROUP BY dept
ORDER BY avg_salary DESC;
EOF

echo ""
echo "  What mmap does:"
echo "  - Without mmap: SQLite calls read() → data copied kernel→userspace"
echo "  - With mmap:    file is mapped into process address space directly"
echo "                  no copy needed, OS page cache IS the SQLite buffer"
echo "  - Result: faster reads, especially for large databases"

# ── SECTION 4: TRACE SYSCALLS WITH STRACE ────────────────
echo ""
echo ">>> Section 4: strace — mmap vs read syscalls"
echo ""

if ! command -v strace &> /dev/null; then
    echo "  strace not installed. Install with: sudo apt install strace"
else
    echo "  --- Without mmap (mmap_size=0) ---"
    strace -e trace=mmap,mmap2,read,pread64 sqlite3 $DB \
        "PRAGMA mmap_size=0; SELECT COUNT(*) FROM employees;" 2>&1 \
        | grep -E "mmap|pread|read\(" | head -10

    echo ""
    echo "  --- With mmap (mmap_size=256MB) ---"
    strace -e trace=mmap,mmap2,read,pread64 sqlite3 $DB \
        "PRAGMA mmap_size=268435456; SELECT COUNT(*) FROM employees;" 2>&1 \
        | grep -E "mmap|pread|read\(" | head -10
fi

# ── SECTION 5: WAL MODE ──────────────────────────────────
echo ""
echo ">>> Section 5: WAL mode vs DELETE (rollback) mode"
echo ""

sqlite3 $DB <<EOF
-- switch to WAL mode
PRAGMA journal_mode=WAL;
PRAGMA journal_mode;

-- in WAL mode, writers don't block readers
-- a -wal file is created alongside the .db file
EOF

echo ""
echo "  Files created in WAL mode:"
ls -lh ${DB}* 2>/dev/null

echo ""
echo "  WAL vs DELETE journal:"
echo "  DELETE (default):"
echo "    - writes original data to a rollback journal before modifying .db"
echo "    - readers blocked during write"
echo "  WAL:"
echo "    - new writes go to a separate -wal file"
echo "    - readers read from .db, writers write to -wal"
echo "    - much better concurrent read performance"

echo ""
echo "============================================"
echo "  Section Complete"
echo "============================================"