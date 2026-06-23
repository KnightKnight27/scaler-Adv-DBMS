"""Shared helpers for the narrated demos.

Importing this module puts MiniDB's `src/` on the path so the demos run from a
fresh checkout without an editable install.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))


def banner(title: str) -> None:
    print("\n" + "=" * 72)
    print(f"  {title}")
    print("=" * 72)


def step(msg: str) -> None:
    print(f"\n--> {msg}")


def show(label: str, value) -> None:
    print(f"      {label}: {value}")


def sql(db, statement: str, echo: bool = True):
    """Run a SQL statement through the engine and print it + its result."""
    if echo:
        print(f"\n  SQL> {statement}")
    result = db.execute(statement)
    if result.columns:
        print("       " + " | ".join(result.columns))
        for row in result.rows:
            print("       " + " | ".join("NULL" if v is None else str(v) for v in row))
        print(f"       ({len(result.rows)} row(s))")
    elif result.message:
        print(f"       {result.message}")
    return result
