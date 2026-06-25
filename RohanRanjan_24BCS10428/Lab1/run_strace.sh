#!/usr/bin/env bash
# Lab 1 helper: build the reader, create the test file, and trace the syscalls.
set -euo pipefail

echo "hello from lab 1" > test.txt
g++ -std=c++17 -O0 -o reader reader.cpp

echo "=== inode of test.txt ==="
ls -i test.txt
stat test.txt

echo
echo "=== strace (file I/O syscalls only) ==="
strace -e trace=openat,read,close,fstat,mmap ./reader
