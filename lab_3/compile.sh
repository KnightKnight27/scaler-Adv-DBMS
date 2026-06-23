#!/bin/bash
# Compile script for Lab 3 - ClockSweep Buffer Pool

echo "Compiling clocksweep.cpp..."
g++ -std=c++17 -O2 -Wall -Wextra -o clocksweep clocksweep.cpp

if [ $? -eq 0 ]; then
    echo "✓ Compilation successful!"
    echo "Run with: ./clocksweep"
else
    echo "✗ Compilation failed!"
    exit 1
fi
