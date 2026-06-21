from typing import Dict, Any

class TransactionManager:
    def __init__(self, lock_manager, wal_manager, recovery_manager=None):
        self.lock_manager = lock_manager
        self.wal_manager = wal_manager
        self.recovery_manager = recovery_manager
        self.next_txn_id = 1
        self.global_timestamp = 0
        
        # Maps txn_id -> Dict of transaction metadata:
        # { "id": int, "state": str ("ACTIVE", "COMMITTED", "ABORTED"), "mode": str ("2PL", "MVCC"), "snapshot_ts": int }
        self.transactions = {}
        self.active_txns = set()

    def begin_transaction(self, mode: str = "2PL") -> int:
        txn_id = self.next_txn_id
        self.next_txn_id += 1
        
        mode = mode.upper()
        if mode not in ("2PL", "MVCC"):
            raise ValueError(f"Invalid transaction mode: {mode}")

        snapshot_ts = self.global_timestamp
        self.transactions[txn_id] = {
            "id": txn_id,
            "state": "ACTIVE",
            "mode": mode,
            "snapshot_ts": snapshot_ts,
            "has_writes": False
        }
        self.active_txns.add(txn_id)
        
        # Append BEGIN to WAL
        if self.wal_manager:
            self.wal_manager.append({
                "txn_id": txn_id,
                "type": "BEGIN",
                "table": None,
                "operation": None,
                "old_value": None,
                "new_value": None
            })

        return txn_id

    def commit(self, txn_id: int):
        if txn_id not in self.transactions:
            raise ValueError(f"Transaction {txn_id} does not exist")
        txn = self.transactions[txn_id]
        if txn["state"] != "ACTIVE":
            raise ValueError(f"Transaction {txn_id} is not active ({txn['state']})")

        # Increment global timestamp if transaction had writes (for MVCC snapshot progression)
        if txn["has_writes"]:
            self.global_timestamp += 1
        txn["commit_ts"] = self.global_timestamp

        # Append COMMIT to WAL and flush
        if self.wal_manager:
            self.wal_manager.append({
                "txn_id": txn_id,
                "type": "COMMIT",
                "table": None,
                "operation": None,
                "old_value": None,
                "new_value": None
            })
            self.wal_manager.flush()

        # Release locks if 2PL mode
        if txn["mode"] == "2PL" and self.lock_manager:
            self.lock_manager.release_locks(txn_id)

        txn["state"] = "COMMITTED"
        if txn_id in self.active_txns:
            self.active_txns.remove(txn_id)

    def rollback(self, txn_id: int):
        if txn_id not in self.transactions:
            raise ValueError(f"Transaction {txn_id} does not exist")
        txn = self.transactions[txn_id]
        if txn["state"] != "ACTIVE":
            raise ValueError(f"Transaction {txn_id} is not active ({txn['state']})")

        # Undo changes using Recovery Manager / WAL
        if self.recovery_manager:
            self.recovery_manager.undo_transaction(txn_id)

        # Append ROLLBACK to WAL and flush
        if self.wal_manager:
            self.wal_manager.append({
                "txn_id": txn_id,
                "type": "ROLLBACK",
                "table": None,
                "operation": None,
                "old_value": None,
                "new_value": None
            })
            self.wal_manager.flush()

        # Release locks if 2PL mode
        if txn["mode"] == "2PL" and self.lock_manager:
            self.lock_manager.release_locks(txn_id)

        txn["state"] = "ABORTED"
        if txn_id in self.active_txns:
            self.active_txns.remove(txn_id)

    def mark_has_writes(self, txn_id: int):
        if txn_id in self.transactions:
            self.transactions[txn_id]["has_writes"] = True

    def get_mode(self, txn_id: int) -> str:
        if txn_id in self.transactions:
            return self.transactions[txn_id]["mode"]
        return "2PL"  # Default

    def get_snapshot_timestamp(self, txn_id: int) -> int:
        if txn_id in self.transactions:
            return self.transactions[txn_id]["snapshot_ts"]
        return 0
