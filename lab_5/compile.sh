#!/bin/bash
# Compile script for Lab 5 - SQL Parser with Shunting-Yard

echo "Compiling sql_parser.cpp..."
g++ -std=c++17 -O2 -Wall -Wextra -o sql_parser sql_parser.cpp

if [ $? -eq 0 ]; then
    echo "✓ Compilation successful!"
    echo "Run with: ./sql_parser"
else
    echo "✗ Compilation failed!"
    exit 1
fi
