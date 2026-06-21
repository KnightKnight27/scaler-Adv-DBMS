"""Shared bootstrap so demos can run with `python demos/demo_*.py`."""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))


def scratch(name: str) -> str:
    d = tempfile.mkdtemp(prefix=f"minidb_{name}_")
    return os.path.join(d, name)


def rule(title: str) -> None:
    print("\n" + "=" * 68)
    print(f"  {title}")
    print("=" * 68)
