#!/usr/bin/env bash
#
# run_strace.sh
#
# Compiles file_io_demo.cpp and runs it under strace, saving the captured
# system-call trace to strace_output.txt.
#
# IMPORTANT: strace is a LINUX-ONLY tool. It works by attaching to a process
# via ptrace(2) and intercepting its system calls. It is not available on
# macOS (use dtruss / dtrace there) or Windows. Run this script on a Linux
# box or inside a Linux container/VM. The committed strace_output.txt was
# captured on Linux and is provided as a reference sample.

set -euo pipefail

SRC="file_io_demo.cpp"
BIN="./file_io_demo"
OUT="strace_output.txt"

# --- 1. Compile -----------------------------------------------------------
echo "[*] Compiling ${SRC} ..."
g++ -std=c++17 "${SRC}" -o file_io_demo

# --- 2. Check that strace exists ------------------------------------------
if ! command -v strace >/dev/null 2>&1; then
    echo "[!] strace not found on this system."
    echo "    strace is Linux-only. On macOS try: sudo dtruss ${BIN}"
    echo "    Running the program normally instead (no trace captured):"
    "${BIN}"
    exit 0
fi

# --- 3. Run under strace --------------------------------------------------
# -f                follow forked/cloned children (so we trace everything)
# -e trace=...      restrict the trace to the file-I/O syscalls we care about
#
# We keep KEEP_FILE=1 so the demo leaves lab1_data.bin on disk for the
# unlink step to remain optional during inspection; remove it to also trace
# unlink. Here we trace the full set including unlink.
echo "[*] Tracing ${BIN} -> ${OUT}"
strace -f \
    -e trace=openat,read,write,lseek,fsync,close,fstat,unlink \
    "${BIN}" 2> "${OUT}"

echo "[*] Trace written to ${OUT}"
echo "[*] Done."
