-- Lab 2: SQLite3 PRAGMA introspection queries

-- Open database
-- sqlite3 students.db

-- Storage internals
PRAGMA page_size;       -- 4096 (bytes per page, set at creation)
PRAGMA page_count;      -- number of allocated pages
PRAGMA freelist_count;  -- unused pages available for reuse

-- File size = page_size * page_count
SELECT page_size * page_count AS file_size_bytes FROM pragma_page_size, pragma_page_count;

-- mmap: disable vs enable
PRAGMA mmap_size;               -- 0 = disabled
PRAGMA mmap_size = 268435456;   -- enable 256 MB memory-mapped I/O
PRAGMA mmap_size;               -- confirm

-- WAL mode (improves concurrent reads)
PRAGMA journal_mode;            -- DELETE (default rollback journal)
PRAGMA journal_mode = WAL;      -- switch to Write-Ahead Logging
PRAGMA journal_mode;            -- WAL

-- Cache
PRAGMA cache_size;              -- pages held in memory (negative = KB)
PRAGMA cache_size = -8000;      -- 8 MB page cache

-- Integrity
PRAGMA integrity_check;         -- validate B-tree structure of all pages
PRAGMA database_list;           -- show attached databases

-- Table info
CREATE TABLE IF NOT EXISTS students (
    id      INTEGER PRIMARY KEY,
    name    TEXT NOT NULL,
    age     INTEGER,
    gpa     REAL
);

INSERT OR IGNORE INTO students VALUES (1, 'Alice', 22, 3.8);
INSERT OR IGNORE INTO students VALUES (2, 'Bob',   25, 2.9);
INSERT OR IGNORE INTO students VALUES (3, 'Carol', 21, 3.5);

-- Query plan (shows B-tree traversal)
EXPLAIN QUERY PLAN SELECT * FROM students WHERE id = 2;
-- SEARCH students USING INTEGER PRIMARY KEY (rowid=?)

EXPLAIN QUERY PLAN SELECT * FROM students WHERE name = 'Alice';
-- SCAN students  (full scan — no index on name)

-- Create index and re-check plan
CREATE INDEX IF NOT EXISTS idx_name ON students(name);
EXPLAIN QUERY PLAN SELECT * FROM students WHERE name = 'Alice';
-- SEARCH students USING INDEX idx_name (name=?)
