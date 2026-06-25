-- Lab 2: SQLite3 storage-internals introspection.
-- Run with:  sqlite3 students.db < pragmas.sql
-- (or paste interactively into `sqlite3 students.db`)

-- A tiny table so the file actually has pages to inspect.
CREATE TABLE IF NOT EXISTS students (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL,
    age   INTEGER,
    gpa   REAL
);
INSERT INTO students (name, age, gpa) VALUES
    ('Alice', 22, 3.8),
    ('Bob',   25, 2.9),
    ('Carol', 21, 3.5),
    ('Dave',  30, 3.1);

-- ── Storage internals ─────────────────────────────────────────────
PRAGMA page_size;     -- default 4096 bytes (matches the OS page size)
PRAGMA page_count;    -- pages currently allocated; file size = page_size * page_count
PRAGMA mmap_size;     -- 0 by default; memory-mapped I/O is off

-- Enable memory-mapped I/O (256 MB) so reads become memory accesses
-- instead of read() syscalls for sequential access.
PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;     -- confirm it took effect

-- ── Other useful introspection ───────────────────────────────────
PRAGMA journal_mode;     -- WAL, DELETE, MEMORY, ...
PRAGMA cache_size;       -- number of pages held in memory
PRAGMA integrity_check;  -- validate all pages
PRAGMA database_list;    -- attached databases

SELECT count(*) AS student_count FROM students;
