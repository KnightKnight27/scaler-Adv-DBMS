import json
from typing import Set

class RecoveryManager:
    def __init__(self, db, wal_manager):
        self.db = db
        self.wal_manager = wal_manager

    def recover(self):
        """Runs ARIES-style recovery on startup: Analysis, Redo, and Undo passes."""
        records = self.wal_manager.get_log_records()
        if not records:
            return

        # 1. Analysis Pass
        active_txns = set()
        for record in records:
            txn_id = record["txn_id"]
            rec_type = record["type"]
            if rec_type == "BEGIN":
                active_txns.add(txn_id)
            elif rec_type in ("COMMIT", "ROLLBACK"):
                if txn_id in active_txns:
                    active_txns.remove(txn_id)

        # 2. Redo Pass
        for record in records:
            rec_type = record["type"]
            if rec_type in ("INSERT", "DELETE", "UPDATE"):
                table_name = record["table"]
                op = record["operation"]  # (page_id, slot_id)
                heap_file = self.db.tables.get(table_name)
                if not heap_file:
                    continue  # Table might not be loaded yet or created in WAL later
                
                page_id, slot_id = op
                if rec_type == "INSERT":
                    val_bytes = json.dumps(record["new_value"]).encode("utf-8")
                    heap_file.write_record_at_slot(page_id, slot_id, val_bytes)
                    self.db.update_indexes_for_insert(table_name, record["new_value"], (page_id, slot_id))
                elif rec_type == "DELETE":
                    heap_file.delete_record(page_id, slot_id)
                    self.db.update_indexes_for_delete(table_name, record["old_value"])
                elif rec_type == "UPDATE":
                    val_bytes = json.dumps(record["new_value"]).encode("utf-8")
                    heap_file.write_record_at_slot(page_id, slot_id, val_bytes)
                    self.db.update_indexes_for_delete(table_name, record["old_value"])
                    self.db.update_indexes_for_insert(table_name, record["new_value"], (page_id, slot_id))

        # 3. Undo Pass (loser transactions)
        for record in reversed(records):
            txn_id = record["txn_id"]
            if txn_id in active_txns:
                self._undo_single_record(record)

    def undo_transaction(self, txn_id: int):
        """Rolls back all changes made by a single transaction."""
        records = self.wal_manager.get_log_records()
        for record in reversed(records):
            if record["txn_id"] == txn_id:
                self._undo_single_record(record)

    def _undo_single_record(self, record: dict):
        rec_type = record["type"]
        if rec_type not in ("INSERT", "DELETE", "UPDATE"):
            return

        table_name = record["table"]
        op = record["operation"]
        page_id, slot_id = op
        heap_file = self.db.tables.get(table_name)
        if not heap_file:
            return

        if rec_type == "INSERT":
            # Inverse of INSERT is DELETE
            heap_file.delete_record(page_id, slot_id)
            self.db.update_indexes_for_delete(table_name, record["new_value"])
        elif rec_type == "DELETE":
            # Inverse of DELETE is INSERT (restore the old value)
            val_bytes = json.dumps(record["old_value"]).encode("utf-8")
            heap_file.write_record_at_slot(page_id, slot_id, val_bytes)
            self.db.update_indexes_for_insert(table_name, record["old_value"], (page_id, slot_id))
        elif rec_type == "UPDATE":
            # Inverse of UPDATE is writing the old value back
            val_bytes = json.dumps(record["old_value"]).encode("utf-8")
            heap_file.write_record_at_slot(page_id, slot_id, val_bytes)
            self.db.update_indexes_for_delete(table_name, record["new_value"])
            self.db.update_indexes_for_insert(table_name, record["old_value"], (page_id, slot_id))

    def create_checkpoint(self, active_txn_ids: Set[int]):
        """Creates a WAL checkpoint record and flushes all dirty pages to disk."""
        if self.wal_manager:
            self.wal_manager.append({
                "txn_id": 0,
                "type": "CHECKPOINT",
                "table": None,
                "operation": None,
                "old_value": None,
                "new_value": list(active_txn_ids)
            })
            self.wal_manager.flush()
        
        # Flush all dirty pages in buffer pool to disk
        self.db.buffer_pool.flush_all()
