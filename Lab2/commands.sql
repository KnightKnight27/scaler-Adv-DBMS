-- Create/Open Database
.open students.db

-- Page Size
PRAGMA page_size;

-- Page Count
PRAGMA page_count;

-- mmap Size (default)
PRAGMA mmap_size;

-- Enable mmap (256 MB)
PRAGMA mmap_size = 268435456;

-- Verify mmap
PRAGMA mmap_size;

-- Journal Mode
PRAGMA journal_mode;

-- Cache Size
PRAGMA cache_size;

-- Integrity Check
PRAGMA integrity_check;

-- Database List
PRAGMA database_list;