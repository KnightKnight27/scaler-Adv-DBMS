#!/usr/bin/env bash
# run_tests.sh — build and verify Assignment 8
# Tanishq Singh | 24BCS10303

set -e

echo "Building..."
make clean && make

echo ""
echo "Running tests..."
./txn_manager

echo ""
echo "All done."
