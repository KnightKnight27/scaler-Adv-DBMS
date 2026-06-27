#!/bin/bash
# ──────────────────────────────────────────────────────────────
# Lab 2: Trace SQLite's mmap and I/O behavior using strace
# ──────────────────────────────────────────────────────────────

set -euo pipefail

DB_FILE="mmap_test.db"
TRACE_LOG="sqlite_strace.log"

echo "╔══════════════════════════════════════════════════════════╗"
echo "║      Lab 2 — SQLite mmap I/O Analysis with strace      ║"
echo "╚══════════════════════════════════════════════════════════╝"

# Cleanup
rm -f ${DB_FILE} ${DB_FILE}-wal ${DB_FILE}-shm ${DB_FILE}-journal

echo ""
echo "[1/4] Creating test database with strace..."

# Trace SQLite3 CLI operations
strace -f -tt -T \
    -e trace=openat,read,write,mmap,munmap,fsync,fdatasync,close,fstat,fcntl,lseek \
    -o ${TRACE_LOG} \
    sqlite3 ${DB_FILE} <<EOF
PRAGMA page_size = 4096;
PRAGMA journal_mode = WAL;
PRAGMA mmap_size = 268435456;

CREATE TABLE test_data (
    id INTEGER PRIMARY KEY,
    value TEXT,
    number REAL
);

-- Insert enough data to see interesting I/O patterns
BEGIN;
$(for i in $(seq 1 1000); do echo "INSERT INTO test_data VALUES ($i, 'row_${i}_data_payload', $i.${i});"; done)
COMMIT;

-- Force a read
SELECT COUNT(*) FROM test_data;
SELECT * FROM test_data WHERE id = 500;

-- Checkpoint WAL
PRAGMA wal_checkpoint(FULL);
EOF

echo ""
echo "[2/4] Analyzing strace output..."

echo ""
echo "=== Syscall Counts ==="
for syscall in openat read write mmap munmap fsync fdatasync close fstat fcntl lseek; do
    count=$(grep -c "${syscall}(" ${TRACE_LOG} 2>/dev/null || echo "0")
    printf "  %-12s : %s calls\n" "${syscall}" "${count}"
done

echo ""
echo "[3/4] Key I/O patterns..."

echo ""
echo "=== mmap() calls (memory-mapped regions) ==="
grep "mmap(" ${TRACE_LOG} | grep -v "PROT_NONE" | head -10

echo ""
echo "=== fsync/fdatasync calls (durability) ==="
grep -E "(fsync|fdatasync)\(" ${TRACE_LOG} | head -10

echo ""
echo "=== File open/close pattern ==="
grep "openat(" ${TRACE_LOG} | grep -E "(${DB_FILE}|wal|shm)" | head -10

echo ""
echo "[4/4] Summary"
echo ""
echo "Full trace saved to: ${TRACE_LOG}"
echo "Trace file size: $(du -h ${TRACE_LOG} | cut -f1)"

# Cleanup
rm -f ${DB_FILE} ${DB_FILE}-wal ${DB_FILE}-shm ${DB_FILE}-journal

echo ""
echo "Done!"
