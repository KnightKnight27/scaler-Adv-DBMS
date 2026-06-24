#!/bin/bash
# ──────────────────────────────────────────────────────────────
# Lab 1: strace runner script
# Compiles and runs file_io_demo.cpp under strace to capture
# the complete syscall trace.
# ──────────────────────────────────────────────────────────────

set -euo pipefail

PROG="file_io_demo"
SRC="file_io_demo.cpp"
TRACE_LOG="strace_output.log"

echo "╔══════════════════════════════════════════════════════════╗"
echo "║          Lab 1 — strace File I/O Tracer                 ║"
echo "╚══════════════════════════════════════════════════════════╝"

# Step 1: Compile
echo ""
echo "[1/3] Compiling ${SRC}..."
g++ -std=c++17 -O0 -g -o ${PROG} ${SRC}
echo "      Compiled successfully → ./${PROG}"

# Step 2: Run under strace
echo ""
echo "[2/3] Running under strace (output → ${TRACE_LOG})..."
echo "      Tracing syscalls: openat, read, write, lseek, fsync, mmap, munmap, close, fstat, msync"
echo ""
strace -f -tt -T \
    -e trace=openat,read,write,lseek,fsync,mmap,munmap,close,fstat,msync,unlink \
    -o ${TRACE_LOG} \
    ./${PROG}

# Step 3: Analyze results
echo ""
echo "[3/3] Syscall summary from trace:"
echo "─────────────────────────────────────────────────"

echo ""
echo "=== Syscall Count ==="
# Count occurrences of each syscall
for syscall in openat read write lseek fsync mmap munmap close fstat msync unlink; do
    count=$(grep -c "^[0-9].*${syscall}(" ${TRACE_LOG} 2>/dev/null || echo "0")
    printf "  %-10s : %s calls\n" "${syscall}" "${count}"
done

echo ""
echo "=== Key Observations ==="
echo ""

# Show write() calls to our test file
echo "--- write() syscalls (data going to page cache) ---"
grep "write(" ${TRACE_LOG} | grep -v "EBADF" | head -10
echo ""

# Show fsync
echo "--- fsync() syscalls (flushing page cache to disk) ---"
grep "fsync(" ${TRACE_LOG} | head -5
echo ""

# Show mmap/munmap
echo "--- mmap()/munmap() syscalls (memory-mapped I/O) ---"
grep -E "(mmap|munmap)\(" ${TRACE_LOG} | head -10
echo ""

echo "Full trace saved to: ${TRACE_LOG}"
echo "View it with: less ${TRACE_LOG}"
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  Trace complete! Analyze ${TRACE_LOG} to follow the     ║"
echo "║  inode → VFS → page cache → syscall journey.           ║"
echo "╚══════════════════════════════════════════════════════════╝"
