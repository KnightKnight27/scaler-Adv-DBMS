"""Per-transaction bookkeeping. The lifecycle (begin/commit/abort) lives in
Database; here we just hold the id, state, the set of locks taken, and an undo
list of callables we replay in reverse on abort. Durability is the WAL's job
(recovery/wal.py); this undo list only reverts in-memory state.
"""

import itertools

ACTIVE = "ACTIVE"
COMMITTED = "COMMITTED"
ABORTED = "ABORTED"
IN_DOUBT = "IN_DOUBT"      # commit fsync failed; real outcome decided at recovery


class Transaction:
    # TODO: process-global, so ids climb across Database instances. fine for one
    # DB; move onto Database if we ever want per-DB id spaces.
    _ids = itertools.count(1)

    def __init__(self):
        self.txn_id = next(self._ids)
        self.state = ACTIVE
        self.undo = []          # callables, applied in reverse on abort
        self.locks = set()

    def __repr__(self):
        return f"<Txn {self.txn_id} {self.state}>"
