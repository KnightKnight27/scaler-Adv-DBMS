"""Smoke test: every demo runs end-to-end with exit code 0."""

import os
import subprocess
import sys

import pytest

DEMOS_DIR = os.path.join(os.path.dirname(__file__), "..", "demos")
DEMOS = [
    "demo_storage", "demo_btree", "demo_query", "demo_optimizer",
    "demo_transactions", "demo_recovery", "demo_lsm",
]


@pytest.mark.parametrize("name", DEMOS)
def test_demo_runs(name):
    path = os.path.join(DEMOS_DIR, name + ".py")
    proc = subprocess.run(
        [sys.executable, path], capture_output=True, text=True, timeout=60
    )
    assert proc.returncode == 0, f"{name} failed:\n{proc.stdout}\n{proc.stderr}"
    assert "Takeaway" in proc.stdout
