from __future__ import annotations

import argparse
from pathlib import Path

from .engine import MiniDBEngine
from .transactions import DeadlockError, TransactionAbortedError


def main() -> None:
    parser = argparse.ArgumentParser(description="MiniDB demo CLI.")
    parser.add_argument(
        "--data-dir",
        default=str(Path.cwd() / "data" / "runtime"),
        help="Directory used for MiniDB catalog, heap files, and index files.",
    )
    parser.add_argument(
        "--sql",
        action="append",
        default=[],
        help="SQL statement to execute. Repeat this flag to run multiple statements.",
    )
    args = parser.parse_args()

    engine = MiniDBEngine(args.data_dir)
    for sql in args.sql:
        try:
            result = engine.execute(sql)
        except (DeadlockError, TransactionAbortedError) as exc:
            print(f"{sql}\n{type(exc).__name__}: {exc}")
            continue
        print(sql)
        print(result)


if __name__ == "__main__":
    main()

