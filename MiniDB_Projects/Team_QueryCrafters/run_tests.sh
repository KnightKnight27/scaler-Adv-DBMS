#!/bin/bash
echo "Running MiniDB End-to-End Tests..."
cd "$(dirname "$0")"
pip3 install pytest -q --break-system-packages 2>/dev/null || true
python3 -m pytest tests/test_end_to_end.py -v --tb=short 2>&1
echo "Done."
