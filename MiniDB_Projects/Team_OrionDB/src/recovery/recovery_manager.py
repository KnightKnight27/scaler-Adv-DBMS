import os
import struct
from src.recovery.wal_manager import LogRecordType

class RecoveryManager:
    def __init__(self, log_filepath, bpm):
        self.log_filepath = log_filepath
        self.bpm = bpm

    def _read_all_records(self):
        records = []
        if not os.path.exists(self.log_filepath):
            return records
            
        with open(self.log_filepath, "rb") as f:
            while True:
                header = f.read(13)  # lsn (8B) + type (1B) + txn_id (4B)
                if len(header) < 13:
                    break
                lsn, rec_type, txn_id = struct.unpack(">QBI", header)
                
                record = {
                    "lsn": lsn,
                    "type": rec_type,
                    "txn_id": txn_id
                }
                
                if rec_type == LogRecordType.UPDATE:
                    update_header = f.read(6)  # page_id (4B) + length (2B)
                    page_id, length = struct.unpack(">IH", update_header)
                    before_bytes = f.read(length)
                    after_bytes = f.read(length)

                    record.update({
                        "page_id": page_id,
                        "length": length,
                        "before_bytes": before_bytes,
                        "after_bytes": after_bytes
                    })
                records.append(record)
        return records

    def run_recovery(self, wal_manager):
        print("[Recovery] Starting crash recovery process...")
        records = self._read_all_records()
        
        # 1. Analysis Phase
        active_txs = set()
        for rec in records:
            rec_type = rec["type"]
            txn_id = rec["txn_id"]
            if rec_type == LogRecordType.BEGIN:
                active_txs.add(txn_id)
            elif rec_type in (LogRecordType.COMMIT, LogRecordType.ABORT):
                if txn_id in active_txs:
                    active_txs.remove(txn_id)
                    
        print(f"[Recovery] Analysis Phase complete. Active transactions at crash (losers): {list(active_txs)}")

        # 2. Redo Phase (Repeating History)
        redo_count = 0
        for rec in records:
            if rec["type"] == LogRecordType.UPDATE:
                page_id = rec["page_id"]
                lsn = rec["lsn"]
                after_bytes = rec["after_bytes"]

                # Fetch page via BPM
                page = self.bpm.fetch_page(page_id)
                page_lsn = page.get_lsn()

                if page_lsn < lsn:
                    # Apply redo: restore full page image
                    page.data[:] = after_bytes
                    page.set_lsn(lsn)
                    self.bpm.unpin_page(page_id, is_dirty=True)
                    redo_count += 1
                else:
                    self.bpm.unpin_page(page_id, is_dirty=False)
                    
        print(f"[Recovery] Redo Phase complete. Replayed {redo_count} update records.")

        # 3. Undo Phase (Rolling back active/loser transactions)
        undo_count = 0
        # Iterate backwards
        for rec in reversed(records):
            txn_id = rec["txn_id"]
            if txn_id in active_txs and rec["type"] == LogRecordType.UPDATE:
                page_id = rec["page_id"]
                before_bytes = rec["before_bytes"]
                after_bytes = rec["after_bytes"]

                # Fetch page
                page = self.bpm.fetch_page(page_id)

                # Apply undo: restore full before-image page
                page.data[:] = before_bytes

                # Log CLR / Undo record to WAL
                new_lsn = wal_manager.log_update(txn_id, page_id, after_bytes, before_bytes)
                page.set_lsn(new_lsn)

                self.bpm.unpin_page(page_id, is_dirty=True)
                undo_count += 1

        # Log ABORT records for all loser transactions to close them out
        for txn_id in active_txs:
            wal_manager.log_abort(txn_id)
            
        # Flush all recovered pages to disk
        self.bpm.flush_all()
        print(f"[Recovery] Undo Phase complete. Rolled back {undo_count} update records.")
        print("[Recovery] Database is recovered and consistent.")
        return active_txs
