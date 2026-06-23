"""MiniDB — a from-scratch relational database engine.

Team walError | Advanced DBMS capstone.

Public API:
    from minidb import Database
    db = Database("my.db")
    result = db.execute("SELECT * FROM t")

The engine is organized as a layered stack (bottom to top):
    disk_manager -> buffer_pool -> heap / btree -> catalog
    -> sql (parser) -> plan (optimizer) -> executor -> engine (facade)
with cross-cutting transaction/lock_manager + wal/recovery, plus an
alongside LSM-tree storage engine (Track C extension).
"""

from .engine import Database, Result

__all__ = ["Database", "Result"]
__version__ = "0.1.0"
