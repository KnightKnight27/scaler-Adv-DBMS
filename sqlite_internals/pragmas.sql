-- Lab 2: SQLite storage-internals introspection.
--   Run: sqlite3 ../students.db ".read pragmas.sql"
PRAGMA page_size;        -- bytes per page (fixed at db creation)
PRAGMA page_count;       -- pages allocated; file size = page_size * page_count
PRAGMA mmap_size;        -- 0 = mmap off; >0 maps the db file into the address space
PRAGMA journal_mode;     -- DELETE / WAL / MEMORY ...
PRAGMA cache_size;       -- pages held in the page cache (negative = KiB)
PRAGMA database_list;    -- attached databases and their files
PRAGMA integrity_check;  -- validate every page
