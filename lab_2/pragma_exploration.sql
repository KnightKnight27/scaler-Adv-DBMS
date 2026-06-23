-- Lab 2: PRAGMA Command Exploration
-- Exploring SQLite3 storage internals

.print "=== SQLite3 PRAGMA Exploration ==="
.print ""

-- 1. Page Size
.print "1. PAGE SIZE"
.print "   Default page size (must match OS page size for optimal performance)"
PRAGMA page_size;
.print ""

-- 2. Page Count
.print "2. PAGE COUNT"
.print "   Total number of pages allocated in the database file"
PRAGMA page_count;
.print ""

-- 3. Database File Size Calculation
.print "3. FILE SIZE = page_size × page_count"
.print "   (calculated programmatically in results)"
.print ""

-- 4. Memory-Mapped I/O Size
.print "4. MMAP SIZE (Current)"
.print "   0 = disabled, >0 = enabled (bytes)"
PRAGMA mmap_size;
.print ""

-- 5. Journal Mode
.print "5. JOURNAL MODE"
.print "   DELETE: traditional rollback journal"
.print "   WAL: Write-Ahead Logging (better concurrency)"
PRAGMA journal_mode;
.print ""

-- 6. Cache Size
.print "6. CACHE SIZE"
.print "   Number of pages held in memory (negative = KB)"
PRAGMA cache_size;
.print ""

-- 7. Page Cache Statistics
.print "7. PAGE CACHE STATS"
PRAGMA page_count;
PRAGMA freelist_count;
.print ""

-- 8. Database List
.print "8. DATABASE LIST"
.print "   All attached databases"
PRAGMA database_list;
.print ""

-- 9. Encoding
.print "9. ENCODING"
PRAGMA encoding;
.print ""

-- 10. Table Information
.print "10. TABLE INFO (students)"
PRAGMA table_info(students);
.print ""

-- 11. Index List
.print "11. INDEX LIST (students)"
PRAGMA index_list(students);
.print ""

-- 12. Integrity Check
.print "12. INTEGRITY CHECK"
PRAGMA integrity_check;
.print ""

-- 13. Quick Check (faster than integrity_check)
.print "13. QUICK CHECK"
PRAGMA quick_check;
.print ""

-- 14. Foreign Keys Status
.print "14. FOREIGN KEYS"
PRAGMA foreign_keys;
.print ""

-- 15. Synchronous Mode
.print "15. SYNCHRONOUS MODE"
.print "   0=OFF, 1=NORMAL, 2=FULL, 3=EXTRA"
PRAGMA synchronous;
.print ""

-- 16. Auto Vacuum Status
.print "16. AUTO VACUUM"
.print "   0=NONE, 1=FULL, 2=INCREMENTAL"
PRAGMA auto_vacuum;
.print ""

-- 17. Schema Version
.print "17. SCHEMA VERSION"
PRAGMA schema_version;
.print ""

-- 18. User Version
.print "18. USER VERSION"
PRAGMA user_version;
.print ""

.print ""
.print "=== End of PRAGMA Exploration ==="
