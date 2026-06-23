#!/bin/bash
# Compile script for Lab 6 - Transaction Manager

echo "Compiling transaction_manager.cpp..."
g++ -std=c++17 -pthread -O2 -Wall -Wextra -o transaction_manager transaction_manager.cpp

if [ $? -eq 0 ]; then
    echo "✓ Compilation successful!"
    echo "Run with: ./transaction_manager"
else
    echo "✗ Compilation failed!"
    exit 1
fi
