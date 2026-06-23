-- Test memory-mapped I/O performance

.print "=== Testing MMAP Performance ==="
.print ""

-- Test 1: MMAP Disabled (default)
.print "Test 1: MMAP DISABLED"
PRAGMA mmap_size;
.print "Running query..."
.timer ON
SELECT COUNT(*) FROM students WHERE gpa > 3.0;
.timer OFF
.print ""

-- Test 2: Enable MMAP (256 MB)
.print "Test 2: ENABLE MMAP (256 MB)"
PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
.print "Running same query..."
.timer ON
SELECT COUNT(*) FROM students WHERE gpa > 3.0;
.timer OFF
.print ""

-- Test 3: Verify MMAP is active
.print "Test 3: VERIFY MMAP STATUS"
PRAGMA mmap_size;
.print ""

.print "=== MMAP Test Complete ==="
