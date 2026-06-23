"""
MiniDB interactive shell.

Usage:
    python -m minidb.cli [--dir PATH] [--isolation 2PL|MVCC]

Meta commands (start with a dot):
    .tables             list tables
    .schema <table>     show a table's columns
    .explain <select>   show the chosen physical plan
    .analyze [table]    refresh optimizer statistics
    .stats              engine statistics (buffer pool, WAL, MVCC)
    .begin .commit .abort   explicit transaction control
    .help               this message
    .quit / .exit       leave (flushes + closes cleanly)

Anything else is treated as a SQL statement and executed (autocommit unless
inside an explicit .begin).
"""
from __future__ import annotations

import argparse
import sys

from .database import Database, TransactionError


BANNER = r"""
 __  __ _       _ ____  ____
|  \/  (_)_ __ (_)  _ \| __ )   MiniDB 1.0
| |\/| | | '_ \| | | | |  _ \   a tiny relational engine
| |  | | | | | | | |_| | |_) |  type .help for commands
|_|  |_|_|_| |_|_|____/|____/
"""


def run_shell(directory: str, isolation: str):
    db = Database(directory, isolation=isolation)
    txn = None
    print(BANNER)
    print(f"opened '{directory}' (isolation={db.isolation})")
    if getattr(db, "last_recovery", None):
        print("recovery:", db.last_recovery)
    while True:
        try:
            prompt = "minidb> " if txn is None else "minidb*> "
            line = input(prompt).strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        if line.startswith("."):
            cmd, *rest = line[1:].split(maxsplit=1)
            arg = rest[0] if rest else ""
            cmd = cmd.lower()
            if cmd in ("quit", "exit"):
                break
            elif cmd == "help":
                print(__doc__)
            elif cmd == "tables":
                for t in db.catalog.tables:
                    print(" ", t)
            elif cmd == "schema":
                info = db.catalog.tables.get(arg)
                if not info:
                    print("no such table")
                else:
                    pk = info.primary_key
                    for c in info.schema.columns:
                        tag = "  PRIMARY KEY" if c.name == pk else ""
                        idx = "  [indexed]" if c.name in info.indexes else ""
                        print(f"  {c.name} {c.type.name}{tag}{idx}")
            elif cmd == "explain":
                try:
                    print(db.explain(arg))
                except Exception as e:
                    print("error:", e)
            elif cmd == "analyze":
                db.analyze(arg or None)
                print("statistics refreshed")
            elif cmd == "stats":
                import json
                print(json.dumps(db.stats(), indent=2))
            elif cmd == "begin":
                if txn is not None:
                    print("already in a transaction")
                else:
                    txn = db.begin()
                    print(f"began {txn}")
            elif cmd == "commit":
                if txn is None:
                    print("no active transaction")
                else:
                    db.commit(txn); txn = None; print("committed")
            elif cmd in ("abort", "rollback"):
                if txn is None:
                    print("no active transaction")
                else:
                    db.abort(txn); txn = None; print("aborted")
            else:
                print("unknown command; .help for list")
            continue
        # SQL
        try:
            result = db.execute(line, txn=txn)
            if result.__class__.__name__ == "Transaction":
                txn = result
                print(f"began {txn}")
            elif result is None:
                txn = None  # commit/abort via SQL ended the txn
            else:
                print(result)
        except TransactionError as e:
            print("transaction error:", e)
            if txn is not None and not txn.is_active():
                txn = None
        except Exception as e:
            print("error:", e)
    if txn is not None and txn.is_active():
        db.abort(txn)
    db.close()
    print("bye")


def main(argv=None):
    p = argparse.ArgumentParser(description="MiniDB interactive shell")
    p.add_argument("--dir", default="./minidb_data", help="database directory")
    p.add_argument("--isolation", default="2PL", choices=["2PL", "MVCC"])
    args = p.parse_args(argv)
    run_shell(args.dir, args.isolation)


if __name__ == "__main__":
    main()
