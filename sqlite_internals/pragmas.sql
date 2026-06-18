-- SQLite storage inspection for Lab 2.
-- Usage from the repository root:
--   sqlite3 students.db ".read sqlite_internals/pragmas.sql"

.headers on
.mode column

.print 'page_size'
PRAGMA page_size;
.print 'page_count'
PRAGMA page_count;
.print 'freelist_count'
PRAGMA freelist_count;
.print 'cache_size'
PRAGMA cache_size;
.print 'mmap_size before'
PRAGMA mmap_size;
.print 'journal_mode'
PRAGMA journal_mode;
.print 'database_list'
PRAGMA database_list;
.print 'integrity_check'
PRAGMA integrity_check;

.print 'mmap_size after enabling 256 MiB'
PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
