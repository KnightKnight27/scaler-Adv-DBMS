"""MiniDB — a minimal relational database engine.

A teaching-grade RDBMS implementing a page-based storage engine, B+Tree
indexing, a SQL parser/optimizer/executor, strict two-phase locking with
deadlock detection, write-ahead logging with crash recovery, and a
primary-replica replication layer (extension Track D).

The package is organised into self-contained subsystems that mirror the
layers of a real database:

    storage/      heap files, page manager, buffer pool
    index/        B+Tree primary/secondary indexes
    catalog/      table metadata, schemas, tuple (de)serialisation
    sql/          parser, logical/physical planner, optimizer, executor
    txn/          transactions, 2PL lock manager, deadlock detection
    wal/          write-ahead log records + ARIES-style recovery
    replication/  primary-replica log shipping (Track D)
    engine.py     the Database facade that wires everything together
    cli.py        an interactive SQL shell
"""

__version__ = "1.0.0"
__all__ = ["__version__"]
