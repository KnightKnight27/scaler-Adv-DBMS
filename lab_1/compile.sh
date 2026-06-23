#!/bin/bash
# Compile script for Lab 1 - File I/O reader

echo "Compiling reader.cpp..."
g++ -std=c++17 -O2 -Wall -Wextra -o reader reader.cpp

if [ $? -eq 0 ]; then
    echo "✓ Compilation successful!"
    echo "Run with: ./reader"
    echo "Trace with: strace -e trace=openat,read,close,fstat,mmap ./reader"
else
    echo "✗ Compilation failed!"
    exit 1
fi
