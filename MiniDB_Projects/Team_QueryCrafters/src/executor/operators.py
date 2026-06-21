import json
from typing import List, Dict, Any, Callable, Tuple, Union
from src.transactions.lock_manager import TransactionAbortException

class Operator:
    def open(self):
        pass
    def next(self) -> Union[Dict[str, Any], None]:
        pass
    def close(self):
        pass

class SeqScan(Operator):
    def __init__(self, *args, **kwargs):
        from src.storage.heap_file import HeapFile
        self.table_name = None
        self.heap_file = None
        self.db = None
        self.txn_id = 0
        self.alias = None
        
        if len(args) >= 1 and args[0].__class__.__name__ == "HeapFile":
            self.heap_file = args[0]
            self.table_name = self.heap_file.table_name
        elif len(args) >= 4:
            self.table_name = args[0]
            self.heap_file = args[1]
            self.db = args[2]
            self.txn_id = args[3]
            if len(args) >= 5:
                self.alias = args[4]
        else:
            if "heap_file" in kwargs:
                self.heap_file = kwargs["heap_file"]
                self.table_name = self.heap_file.table_name
            if "table_name" in kwargs: self.table_name = kwargs["table_name"]
            if "db" in kwargs: self.db = kwargs["db"]
            if "txn_id" in kwargs: self.txn_id = kwargs["txn_id"]
            if "alias" in kwargs: self.alias = kwargs["alias"]
        self.scanner = None

    def open(self):
        self.scanner = self.heap_file.scan_all()

    def next(self) -> Dict[str, Any]:
        if self.scanner is None:
            return None

        while True:
            try:
                rid, raw_bytes = next(self.scanner)
                page_id, slot_id = rid
            except StopIteration:
                return None

            try:
                record_version = json.loads(raw_bytes.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError, AttributeError):
                # raw_bytes might be dict already (from test payload)
                if isinstance(raw_bytes, dict):
                    record_version = raw_bytes
                else:
                    continue

            # Check if record has standard version wrapper
            if isinstance(record_version, dict) and "_data" in record_version:
                payload = record_version["_data"]
                mode = self.db.transaction_manager.get_mode(self.txn_id) if self.db else "2PL"
                if mode == "MVCC":
                    if self.db and not self.db.mvcc_manager.is_version_visible(record_version, self.txn_id):
                        continue
                else:
                    if self.db:
                        pk_col = self.db.get_primary_key_column(self.table_name)
                        if pk_col and pk_col in payload:
                            pk_val = payload[pk_col]
                            self.db.lock_manager.acquire_lock(self.txn_id, f"row:{self.table_name}:{pk_val}", "SHARED")
            else:
                payload = record_version

            # Construct row dictionary
            row = {}
            for col_name, val in payload.items():
                row[col_name] = val
                row[f"{self.table_name}.{col_name}"] = val
                if self.alias:
                    row[f"{self.alias}.{col_name}"] = val
            
            row["_rid"] = (page_id, slot_id)
            row["_raw_version"] = record_version
            return row

    def close(self):
        self.scanner = None

class IndexScan(Operator):
    def __init__(self, *args, **kwargs):
        from src.storage.heap_file import HeapFile
        from src.indexing.bplus_tree import BPlusTree
        
        self.table_name = None
        self.heap_file = None
        self.db = None
        self.txn_id = 0
        self.alias = None
        self.index = None
        self.index_col = None
        self.op = "="
        self.val = None
        self.rids = []
        self.idx = 0
        
        if len(args) >= 3 and args[0].__class__.__name__ == "HeapFile" and args[2].__class__.__name__ == "BPlusTree":
            self.heap_file = args[0]
            self.table_name = self.heap_file.table_name
            self.index = args[2]
            self.val = kwargs.get("key")
            self.op = "="
        elif len(args) >= 9:
            self.table_name = args[0]
            self.heap_file = args[1]
            self.db = args[2]
            self.txn_id = args[3]
            self.alias = args[4]
            self.index = args[5]
            self.index_col = args[6]
            self.op = args[7]
            self.val = args[8]

    def open(self):
        self.idx = 0
        if self.op == "=":
            rid = self.index.search(self.val)
            self.rids = [rid] if rid is not None else []
        elif self.op == ">":
            self.rids = self.index.range_scan(self.val, None)
        elif self.op == ">=":
            self.rids = self.index.range_scan(self.val, None)
        elif self.op == "<":
            self.rids = self.index.range_scan(None, self.val)
        elif self.op == "<=":
            self.rids = self.index.range_scan(None, self.val)
        else:
            self.rids = []

    def next(self) -> Dict[str, Any]:
        while self.idx < len(self.rids):
            page_id, slot_id = self.rids[self.idx]
            self.idx += 1

            raw_bytes = self.heap_file.get_record(page_id, slot_id)
            if raw_bytes is None:
                continue

            try:
                record_version = json.loads(raw_bytes.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError, AttributeError):
                if isinstance(raw_bytes, dict):
                    record_version = raw_bytes
                else:
                    continue

            if isinstance(record_version, dict) and "_data" in record_version:
                payload = record_version["_data"]
                mode = self.db.transaction_manager.get_mode(self.txn_id) if self.db else "2PL"
                if mode == "MVCC":
                    if self.db and not self.db.mvcc_manager.is_version_visible(record_version, self.txn_id):
                        continue
                else:
                    if self.db:
                        pk_col = self.db.get_primary_key_column(self.table_name)
                        if pk_col and pk_col in payload:
                            pk_val = payload[pk_col]
                            self.db.lock_manager.acquire_lock(self.txn_id, f"row:{self.table_name}:{pk_val}", "SHARED")
            else:
                payload = record_version
            
            # Apply operator post-filtering (B+ tree range scan is inclusive, so we filter strictly if > or <)
            if self.index_col:
                row_val = payload.get(self.index_col)
                if row_val is None:
                    continue
                if self.op == ">" and not (row_val > self.val):
                    continue
                if self.op == "<" and not (row_val < self.val):
                    continue

            # Construct row
            row = {}
            for col_name, val in payload.items():
                row[col_name] = val
                row[f"{self.table_name}.{col_name}"] = val
                if self.alias:
                    row[f"{self.alias}.{col_name}"] = val
            
            row["_rid"] = (page_id, slot_id)
            row["_raw_version"] = record_version
            return row

        return None

    def close(self):
        self.rids = []

class Filter(Operator):
    def __init__(self, child: Operator, predicate_fn: Callable[[Dict[str, Any]], bool]):
        self.child = child
        self.predicate_fn = predicate_fn

    def open(self):
        self.child.open()

    def next(self) -> Dict[str, Any]:
        while True:
            row = self.child.next()
            if row is None:
                return None
            if self.predicate_fn(row):
                return row

    def close(self):
        self.child.close()

class Projection(Operator):
    def __init__(self, child: Operator, columns: List[str]):
        self.child = child
        self.columns = columns

    def open(self):
        self.child.open()

    def next(self) -> Dict[str, Any]:
        row = self.child.next()
        if row is None:
            return None

        if self.columns == ["*"]:
            return row

        projected = {}
        for col in self.columns:
            bare_col = col.split(".")[-1]
            if col in row:
                projected[col] = row[col]
                projected[bare_col] = row[col]
            else:
                # Fallback to column name if fully qualified
                if bare_col in row:
                    projected[col] = row[bare_col]
                    projected[bare_col] = row[bare_col]
                else:
                    projected[col] = None
                    projected[bare_col] = None
        return projected

    def close(self):
        self.child.close()

class NestedLoopJoin(Operator):
    def __init__(self, left: Operator, right: Operator, left_col: str = None, right_col: str = None, condition: Callable = None):
        self.left = left
        self.right = right
        self.left_col = left_col
        self.right_col = right_col
        self.condition = condition
        self.left_row = None

    def open(self):
        self.left.open()
        self.left_row = self.left.next()
        self.right.open()

    def next(self) -> Dict[str, Any]:
        while self.left_row is not None:
            right_row = self.right.next()
            if right_row is None:
                # Reset right, advance left
                self.right.close()
                self.right.open()
                self.left_row = self.left.next()
                continue

            if self.condition:
                if self.condition(self.left_row, right_row):
                    combined = dict(self.left_row)
                    for k, v in right_row.items():
                        if k not in combined:
                            combined[k] = v
                    return combined
            else:
                # Compare keys
                left_val = self.left_row.get(self.left_col)
                right_val = right_row.get(self.right_col)
                
                # If not found directly, try bare name lookups
                if left_val is None:
                    bare_left = self.left_col.split(".")[-1]
                    left_val = self.left_row.get(bare_left)
                if right_val is None:
                    bare_right = self.right_col.split(".")[-1]
                    right_val = right_row.get(bare_right)

                if left_val is not None and right_val is not None and left_val == right_val:
                    combined = dict(self.left_row)
                    for k, v in right_row.items():
                        if k not in combined:
                            combined[k] = v
                    return combined

        return None

    def close(self):
        self.left.close()
        self.right.close()

class Insert(Operator):
    def __init__(self, table_name: str, heap_file, db, txn_id: int, values: list):
        self.table_name = table_name
        self.heap_file = heap_file
        self.db = db
        self.txn_id = txn_id
        self.values = values
        self.executed = False

    def open(self):
        self.executed = False

    def next(self) -> Dict[str, Any]:
        if self.executed:
            return None

        # Build payload
        cols = self.db.schemas[self.table_name]
        if len(self.values) != len(cols):
            raise ValueError(f"Insert value count mismatch. Expected {len(cols)}, got {len(self.values)}")

        payload = {}
        for col, val in zip(cols, self.values):
            payload[col["name"]] = val

        mode = self.db.transaction_manager.get_mode(self.txn_id)
        
        # 2PL: lock the table for write
        if mode == "2PL":
            self.db.lock_manager.acquire_lock(self.txn_id, f"table:{self.table_name}", "EXCLUSIVE")
        else:
            # MVCC: track writes
            self.db.transaction_manager.mark_has_writes(self.txn_id)

        # Structure the record version
        record_version = {
            "_data": payload,
            "created_by_txn": self.txn_id,
            "deleted_by_txn": None,
            "begin_ts": self.txn_id,  # Logical tx ID as creation timestamp
            "end_ts": None
        }

        # Insert to Heap
        raw_bytes = json.dumps(record_version).encode("utf-8")
        page_id, slot_id = self.heap_file.insert_record(raw_bytes)

        # Write to WAL
        if self.db.wal_manager:
            wal_record = {
                "txn_id": self.txn_id,
                "type": "INSERT",
                "table": self.table_name,
                "operation": [page_id, slot_id],
                "old_value": None,
                "new_value": record_version
            }
            self.db.wal_manager.append(wal_record)

        # Update indexes
        self.db.update_indexes_for_insert(self.table_name, record_version, (page_id, slot_id))

        self.executed = True
        return {"rows_inserted": 1}

    def close(self):
        pass

class Delete(Operator):
    def __init__(self, table_name: str, heap_file, db, txn_id: int, predicate_fn: Callable[[Dict[str, Any]], bool]):
        self.table_name = table_name
        self.heap_file = heap_file
        self.db = db
        self.txn_id = txn_id
        self.predicate_fn = predicate_fn
        self.executed = False

    def open(self):
        self.executed = False

    def next(self) -> Dict[str, Any]:
        if self.executed:
            return None

        # We first scan the table to find records that match the predicate
        scan_op = SeqScan(self.table_name, self.heap_file, self.db, self.txn_id)
        scan_op.open()

        matching_rows = []
        while True:
            row = scan_op.next()
            if row is None:
                break
            if self.predicate_fn is None or self.predicate_fn(row):
                matching_rows.append(row)
        scan_op.close()

        deleted_count = 0
        mode = self.db.transaction_manager.get_mode(self.txn_id)

        for row in matching_rows:
            page_id, slot_id = row["_rid"]
            record_version = row["_raw_version"]
            payload = record_version["_data"]
            pk_col = self.db.get_primary_key_column(self.table_name)
            pk_val = payload.get(pk_col) if pk_col else None

            if mode == "2PL":
                # Lock row (or table) for write
                if pk_val is not None:
                    self.db.lock_manager.acquire_lock(self.txn_id, f"row:{self.table_name}:{pk_val}", "EXCLUSIVE")
                else:
                    self.db.lock_manager.acquire_lock(self.txn_id, f"table:{self.table_name}", "EXCLUSIVE")

                # Physically delete the record from Heap
                self.heap_file.delete_record(page_id, slot_id)

                # Write to WAL
                if self.db.wal_manager:
                    self.db.wal_manager.append({
                        "txn_id": self.txn_id,
                        "type": "DELETE",
                        "table": self.table_name,
                        "operation": [page_id, slot_id],
                        "old_value": record_version,
                        "new_value": None
                    })

                # Remove from indexes
                self.db.update_indexes_for_delete(self.table_name, record_version)
            else:
                # MVCC mode: first-committer-wins check
                self.db.mvcc_manager.check_write_write_conflict(record_version, self.txn_id)
                self.db.transaction_manager.mark_has_writes(self.txn_id)

                # Logic: set deleted_by_txn to current transaction
                updated_version = dict(record_version)
                updated_version["deleted_by_txn"] = self.txn_id
                
                # Write record version back
                raw_bytes = json.dumps(updated_version).encode("utf-8")
                self.heap_file.write_record_at_slot(page_id, slot_id, raw_bytes)

                # Write to WAL
                if self.db.wal_manager:
                    self.db.wal_manager.append({
                        "txn_id": self.txn_id,
                        "type": "DELETE",
                        "table": self.table_name,
                        "operation": [page_id, slot_id],
                        "old_value": record_version,
                        "new_value": updated_version
                    })

                # Note: Under MVCC, we do not remove key from B+ tree immediately
                # because readers might still need to traverse it to find visible versions.
                # The index points to the slot, and visibility check happens when fetching the slot.
                # So we keep it in index!

            deleted_count += 1

        self.executed = True
        return {"rows_deleted": deleted_count}

    def close(self):
        pass
