#!/bin/bash
# Compile script for Lab 2 - SQLite3 demo

echo "Compiling sqlite_demo.cpp..."
g++ -std=c++17 -O2 -Wall -Wextra -o sqlite_demo sqlite_demo.cpp -lsqlite3

if [ $? -eq 0 ]; then
    echo "✓ Compilation successful!"
    echo "Run with: ./sqlite_demo"
else
    echo "✗ Compilation failed!"
    exit 1
fi
