"""Execution context shared by the optimizer, operators, and executor.

Bundles the catalog and the current transaction, and centralises lock
acquisition so every access path takes locks the same way (strict 2PL):
shared locks for reads, exclusive for writes, both at table granularity.
"""

from __future__ import annotations

from ..txn.lock_manager import LockMode


class ExecutionContext:
    def __init__(self, catalog, txn=None):
        self.catalog = catalog
        self.txn = txn

    def lock_shared(self, table_name: str) -> None:
        if self.txn is not None:
            self.txn.acquire(f"table:{table_name}", LockMode.S)

    def lock_exclusive(self, table_name: str) -> None:
        if self.txn is not None:
            self.txn.acquire(f"table:{table_name}", LockMode.X)
