#!/bin/bash
# Compile script for Lab 4 - Red-Black Tree & B-Tree

echo "=== Compiling Lab 4 Data Structures ==="
echo ""

echo "1. Compiling Red-Black Tree..."
g++ -std=c++17 -O2 -Wall -Wextra -o red_black_tree red_black_tree.cpp

if [ $? -eq 0 ]; then
    echo "   ✓ Red-Black Tree compilation successful!"
else
    echo "   ✗ Red-Black Tree compilation failed!"
    exit 1
fi

echo ""
echo "2. Compiling B-Tree..."
g++ -std=c++17 -O2 -Wall -Wextra -o btree btree.cpp

if [ $? -eq 0 ]; then
    echo "   ✓ B-Tree compilation successful!"
else
    echo "   ✗ B-Tree compilation failed!"
    exit 1
fi

echo ""
echo "✓ All compilations successful!"
echo ""
echo "Run Red-Black Tree: ./red_black_tree"
echo "Run B-Tree:         ./btree"
