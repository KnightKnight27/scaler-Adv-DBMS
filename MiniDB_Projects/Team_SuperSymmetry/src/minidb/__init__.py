"""
MiniDB — a small but complete relational database engine.

Public entry points:

    from minidb import Database
    db = Database("./mydata", isolation="2PL")   # or "MVCC"
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
    db.execute("INSERT INTO t VALUES (1, 'hello')")
    print(db.execute("SELECT * FROM t"))
    db.close()

Layers (bottom to top):
    disk_manager  -> page-addressable files
    page          -> slotted page layout
    buffer_pool   -> LRU cache enforcing the WAL rule
    heap_file     -> unordered record storage (RID = page,slot)
    btree         -> B+ tree secondary/primary index
    wal           -> write-ahead log + ARIES-style recovery
    lock_manager  -> strict 2PL with deadlock detection
    mvcc          -> multi-version store (snapshot isolation)
    types/sql     -> schema + SQL parser
    catalog       -> table metadata + statistics
    executor      -> Volcano-model physical operators
    optimizer     -> cost-based plan selection
    database      -> the integration layer / public API
"""
from .database import Database, Result, TransactionError, connect
from .transaction import Transaction

__all__ = ["Database", "Result", "Transaction", "TransactionError", "connect"]
__version__ = "1.0.0"
