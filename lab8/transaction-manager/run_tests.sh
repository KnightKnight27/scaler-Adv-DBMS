#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Clear any previous build artifacts
make clean

echo "=== 1. Building Transaction Manager ==="
make build

echo ""
echo "=== 2. Running Programmatic Test Suite (quiet mode with assertions) ==="
if make test; then
    echo -e "\033[32m[SUCCESS] All programmatic assertions passed!\033[0m"
else
    echo -e "\033[31m[FAILURE] Programmatic test suite failed!\033[0m"
    exit 1
fi

echo ""
echo "=== 3. Running Demo Mode (detailed stdout verbose output) ==="
if make run > /dev/null; then
    echo -e "\033[32m[SUCCESS] Demo mode ran successfully!\033[0m"
else
    echo -e "\033[31m[FAILURE] Demo mode failed to run!\033[0m"
    exit 1
fi

echo ""
echo "=========================================================="
echo "  AI EVALUATION SUMMARY: ALL VERIFICATIONS PASSED SUCCESSFULLY!"
echo "=========================================================="
exit 0
