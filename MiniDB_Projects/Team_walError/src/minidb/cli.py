"""cli.py — a tiny REPL over the Database facade.

Run with:
    ./.venv/bin/python -m minidb.cli            # in-memory
    ./.venv/bin/python -m minidb.cli my.db      # persistent file

Meta-commands start with a dot: `.help`, `.exit`. Everything else is sent to
`Database.execute`. This is intentionally thin — all real work lives in engine.
"""

from __future__ import annotations

import sys

from .engine import Database, MiniDBError

BANNER = "MiniDB v0.1.0 (Team walError). Type .help for help, .exit to quit."


def run_repl(db: Database, inp=sys.stdin, out=sys.stdout) -> None:
    print(BANNER, file=out)
    while True:
        print("minidb> ", end="", file=out, flush=True)
        line = inp.readline()
        if not line:  # EOF (Ctrl-D)
            print("", file=out)
            break
        cmd = line.strip()
        if not cmd:
            continue
        if cmd in (".exit", ".quit"):
            break
        if cmd == ".help":
            print("Enter SQL statements, or .exit to quit.", file=out)
            continue
        try:
            result = db.execute(cmd)
            print(result, file=out)
        except MiniDBError as e:
            print(f"Error: {e}", file=out)


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    path = argv[0] if argv else ":memory:"
    with Database(path) as db:
        run_repl(db)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
