from typing import Dict, Any, Union
from src.transactions.lock_manager import TransactionAbortException

class MVCCManager:
    def __init__(self, txn_manager=None):
        self.txn_manager = txn_manager
        self.db_store = {}  # (table, key) -> list of version dicts
        self.txns = {}      # txn_id -> {"state": "ACTIVE"/"COMMITTED", "ts": start_ts, "commit_ts": None}

    def is_version_visible(self, version: dict, txn_id: int) -> bool:
        """
        Visibility rule: a record version is visible to txn T if:
        1. It was created by T, or created by a txn that committed before T's snapshot_ts.
        2. It was NOT deleted, or deleted by a txn that is not committed yet, or deleted by a txn that committed after T's snapshot_ts.
        """
        # If no transaction context (e.g., autocommit or utility scan), see all committed data
        if not txn_id or txn_id not in self.txn_manager.transactions:
            created_by = version.get("created_by_txn")
            if created_by:
                created_txn = self.txn_manager.transactions.get(created_by)
                if not created_txn or created_txn["state"] != "COMMITTED":
                    return False
            
            deleted_by = version.get("deleted_by_txn")
            if deleted_by:
                deleted_txn = self.txn_manager.transactions.get(deleted_by)
                if deleted_txn and deleted_txn["state"] == "COMMITTED":
                    return False
            return True

        txn = self.txn_manager.transactions[txn_id]
        our_snapshot_ts = txn["snapshot_ts"]

        # 1. Check Creation Visibility
        created_by = version.get("created_by_txn")
        if created_by == txn_id:
            # We created it, so we can see it
            pass
        else:
            created_txn = self.txn_manager.transactions.get(created_by)
            if not created_txn or created_txn["state"] != "COMMITTED":
                # Created by an active or aborted transaction (not us): invisible
                return False
            
            # Must have committed before our snapshot timestamp
            created_commit_ts = created_txn.get("commit_ts")
            if created_commit_ts is None or created_commit_ts > our_snapshot_ts:
                return False

        # 2. Check Deletion Visibility
        deleted_by = version.get("deleted_by_txn")
        if deleted_by is None:
            return True  # Not deleted at all

        if deleted_by == txn_id:
            # Deleted by us in this transaction: invisible
            return False

        deleted_txn = self.txn_manager.transactions.get(deleted_by)
        if not deleted_txn or deleted_txn["state"] != "COMMITTED":
            # Deleted by active or aborted transaction: still visible to us
            return True

        # Deleted by committed transaction: check commit time
        deleted_commit_ts = deleted_txn.get("commit_ts")
        if deleted_commit_ts is None or deleted_commit_ts <= our_snapshot_ts:
            # Deleted before our snapshot timestamp: invisible
            return False

        return True

    def check_write_write_conflict(self, version: dict, txn_id: int):
        """
        First-committer-wins rule:
        If a record has been deleted or updated by a concurrent active or committed transaction, abort.
        """
        deleted_by = version.get("deleted_by_txn")
        if deleted_by is not None and deleted_by != txn_id:
            deleted_txn = self.txn_manager.transactions.get(deleted_by)
            if deleted_txn and deleted_txn["state"] in ("ACTIVE", "COMMITTED"):
                raise TransactionAbortException(
                    f"Write-write conflict: Record version was modified by concurrent transaction {deleted_by}."
                )

    def run_garbage_collection(self, db):
        """
        Garbage collection: physically remove record versions where end_ts (deletion commit time)
        is older than the snapshot timestamp of all active transactions.
        """
        # Find oldest active snapshot timestamp
        active_snapshots = [
            t["snapshot_ts"]
            for t in self.txn_manager.transactions.values()
            if t["state"] == "ACTIVE"
        ]
        
        # If no active transactions, oldest snapshot is current timestamp
        oldest_active_ts = min(active_snapshots) if active_snapshots else self.txn_manager.global_timestamp

        for table_name, heap_file in db.tables.items():
            num_pages = heap_file.get_num_pages()
            for page_id in range(num_pages):
                page = db.buffer_pool.fetch_page(page_id, heap_file)
                page_modified = False
                
                for slot_id in range(len(page.slots)):
                    record_bytes = page.get_record(slot_id)
                    if record_bytes is None:
                        continue
                    
                    try:
                        import json
                        version = json.loads(record_bytes.decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue

                    deleted_by = version.get("deleted_by_txn")
                    if deleted_by:
                        deleted_txn = self.txn_manager.transactions.get(deleted_by)
                        if deleted_txn and deleted_txn["state"] == "COMMITTED":
                            deleted_commit_ts = deleted_txn.get("commit_ts")
                            if deleted_commit_ts is not None and deleted_commit_ts < oldest_active_ts:
                                # This version is dead for all active/future transactions!
                                # Physically remove it.
                                page.delete_record(slot_id)
                                page_modified = True
                
                db.buffer_pool.unpin_page(page_id, heap_file, is_dirty=page_modified)

    def insert(self, txn_id: int, table: str, key: Any, data: dict, ts: int):
        if txn_id not in self.txns:
            self.txns[txn_id] = {"state": "ACTIVE", "ts": ts, "commit_ts": None}
        version = {
            "data": data,
            "created_by_txn": txn_id,
            "created_ts": ts,
            "deleted_by_txn": None,
            "deleted_ts": None
        }
        pair_key = (table, key)
        if pair_key not in self.db_store:
            self.db_store[pair_key] = []
        self.db_store[pair_key].append(version)

    def delete(self, txn_id: int, table: str, key: Any, ts: int):
        if txn_id not in self.txns:
            self.txns[txn_id] = {"state": "ACTIVE", "ts": ts, "commit_ts": None}
        pair_key = (table, key)
        if pair_key in self.db_store:
            for version in self.db_store[pair_key]:
                if version["deleted_by_txn"] is None:
                    version["deleted_by_txn"] = txn_id
                    version["deleted_ts"] = ts
                    break

    def commit(self, txn_id: int, commit_ts: int):
        if txn_id in self.txns:
            self.txns[txn_id]["state"] = "COMMITTED"
            self.txns[txn_id]["commit_ts"] = commit_ts

    def read(self, table: str, key: Any, snapshot_ts: int) -> Union[dict, None]:
        pair_key = (table, key)
        if pair_key not in self.db_store:
            return None
        for version in self.db_store[pair_key]:
            created_by = version["created_by_txn"]
            created_txn = self.txns.get(created_by)
            if not created_txn:
                continue
            if created_txn["state"] != "COMMITTED" or created_txn["commit_ts"] > snapshot_ts:
                continue
            deleted_by = version["deleted_by_txn"]
            if deleted_by is not None:
                deleted_txn = self.txns.get(deleted_by)
                if deleted_txn and deleted_txn["state"] == "COMMITTED" and deleted_txn["commit_ts"] <= snapshot_ts:
                    continue
            return version["data"]
        return None
