import os
import json
import base64
from collections import defaultdict

from .page import Page, PAGE_SIZE
from .buffer_pool import BufferPool
from .heapfile import HeapFile
from .wal import WAL
from .transaction import TransactionManager, Transaction, WriteWriteConflict
from .index import BPlusTree
from .parser import SQLParser
from .optimizer import CostBasedOptimizer
from .executor import (
    TableScanOperator,
    IndexScanOperator,
    FilterOperator,
    NestedLoopJoinOperator,
    ProjectOperator
)

class DuplicateKeyError(Exception):
    pass

class Database:
    def __init__(self, data_dir):
        self.data_dir = data_dir
        if not os.path.exists(data_dir):
            os.makedirs(data_dir)
            
        self.catalog_path = os.path.join(data_dir, "catalog.json")
        self.wal_path = os.path.join(data_dir, "minidb.wal")
        
        # Load catalog
        if os.path.exists(self.catalog_path):
            with open(self.catalog_path, "r") as f:
                self.catalog = json.load(f)
        else:
            self.catalog = {"tables": {}}
            self.save_catalog()

        # Components
        self.tm = TransactionManager()
        self.bp = BufferPool(capacity=16)
        self.wal = WAL(self.wal_path)
        self.heap_files = {}
        self.indexes = {}  # (table_name, col_name) -> BPlusTree
        self.tx_ops = defaultdict(list)  # tx_id -> list of operations for in-memory rollback

        # Initialize heap files
        for table_name in self.catalog["tables"]:
            self.heap_files[table_name] = HeapFile(os.path.join(data_dir, f"{table_name}.db"))
            
        # Run crash recovery before accepting statements
        self.recover()

        # Rebuild indexes in memory
        self.rebuild_indexes()

    def save_catalog(self):
        with open(self.catalog_path, "w") as f:
            json.dump(self.catalog, f, indent=2)

    def get_heap_file(self, table_name):
        if table_name not in self.heap_files:
            self.heap_files[table_name] = HeapFile(os.path.join(self.data_dir, f"{table_name}.db"))
        return self.heap_files[table_name]

    def get_page(self, table_name, page_no):
        hf = self.get_heap_file(table_name)
        # Using buffer pool to cache pages
        return self.bp.get(page_no, hf.read_page, hf.write_page)

    def mark_page_dirty(self, table_name, page_no):
        # We need to compute global/unique page_no key for buffer pool
        # For simplicity, since the buffer pool is shared, we use (table_name, page_no) as the key!
        # Let's check how buffer pool key is stored: buffer_pool.py uses page_no keys.
        # To support multiple tables, we must wrap page_no as (table_name, page_no)!
        # Let's adapt our usage to pass (table_name, page_no) as the key to BufferPool.
        pass

    # Let's override get_page to handle (table_name, page_no) key in BufferPool!
    # Let's rewrite get_page and mark_page_dirty carefully:
    def get_page(self, table_name, page_no):
        hf = self.get_heap_file(table_name)
        key = (table_name, page_no)
        
        def load_fn(k):
            return hf.read_page(k[1])
            
        def write_fn(k, page):
            self.get_heap_file(k[0]).write_page(k[1], page)
            
        return self.bp.get(key, load_fn, write_fn)

    def mark_page_dirty(self, table_name, page_no):
        key = (table_name, page_no)
        self.bp.mark_dirty(key)

    def flush_all_dirty(self):
        def write_fn(k, page):
            self.get_heap_file(k[0]).write_page(k[1], page)
        self.bp.flush_all(write_fn)

    def get_index(self, table_name, col_name):
        return self.indexes.get((table_name, col_name))

    def create_table(self, name, schema, primary_key):
        """
        schema: dict of col_name -> type (e.g. "INT", "VARCHAR")
        """
        if name in self.catalog["tables"]:
            return f"Table {name} already exists."
        self.catalog["tables"][name] = {
            "schema": schema,
            "primary_key": primary_key,
            "indexes": [primary_key]  # Primary key is always indexed
        }
        self.save_catalog()
        self.heap_files[name] = HeapFile(os.path.join(self.data_dir, f"{name}.db"))
        # Auto-create B+ Tree for PK
        self.indexes[(name, primary_key)] = BPlusTree()
        return f"Table {name} created successfully."

    def create_index(self, table_name, col_name):
        if table_name not in self.catalog["tables"]:
            raise ValueError(f"Table {table_name} does not exist.")
        if col_name not in self.catalog["tables"][table_name]["schema"]:
            raise ValueError(f"Column {col_name} does not exist in table {table_name}.")
            
        if col_name not in self.catalog["tables"][table_name]["indexes"]:
            self.catalog["tables"][table_name]["indexes"].append(col_name)
            self.save_catalog()
            
        # Rebuild index
        self.rebuild_indexes()
        return f"Index on {table_name}({col_name}) created successfully."

    def serialize_record(self, table_name, data_dict):
        # We write fields in order defined by schema to verify type, then dump to JSON
        schema = self.catalog["tables"][table_name]["schema"]
        ordered_data = {}
        for col, col_type in schema.items():
            val = data_dict.get(col)
            if val is not None:
                if col_type == "INT":
                    ordered_data[col] = int(val)
                else:
                    ordered_data[col] = str(val)
            else:
                ordered_data[col] = None
        return json.dumps(ordered_data).encode('utf-8')

    def deserialize_record(self, table_name, payload):
        return json.loads(payload.decode('utf-8'))

    def rebuild_indexes(self):
        # Scan committed data to build in-memory indexes
        self.indexes = {}
        # Special mock transaction that sees everything committed
        mock_tx = Transaction(999999, set())
        for table_name, table_info in self.catalog["tables"].items():
            indexes = table_info.get("indexes", [])
            for col in indexes:
                self.indexes[(table_name, col)] = BPlusTree()
                # Scan table
                scan = TableScanOperator(self, table_name, mock_tx)
                scan.open()
                while True:
                    row = scan.next()
                    if row is None:
                        break
                    key = row[col]
                    rid = row["_rid"]
                    self.indexes[(table_name, col)].insert(key, rid)
                scan.close()

    def recover(self):
        # ARIES-style crash recovery: REDO then UNDO
        if not os.path.exists(self.wal_path):
            return
            
        active_txs = set()
        committed_txs = set()
        aborted_txs = set()
        
        log_records = []
        # 1. Analysis phase
        for record in self.wal.iter_records():
            log_records.append(record)
            tx_id = record["tx_id"]
            if record["type"] == "BEGIN":
                active_txs.add(tx_id)
            elif record["type"] == "COMMIT":
                active_txs.discard(tx_id)
                committed_txs.add(tx_id)
            elif record["type"] == "ABORT":
                active_txs.discard(tx_id)
                aborted_txs.add(tx_id)

        # 2. REDO phase (repeating history)
        for record in log_records:
            tx_id = record["tx_id"]
            rtype = record["type"]
            if rtype == "INSERT":
                table_name = record["table"]
                page_no = record["page_no"]
                slot_id = record["slot_id"]
                offset = record["offset"]
                data_bytes = base64.b64decode(record["data"])
                
                # Apply change physically to the page
                page = self.get_page(table_name, page_no)
                page.write_record_at(slot_id, offset, data_bytes, xmin=tx_id, xmax=0)
                self.mark_page_dirty(table_name, page_no)
                
            elif rtype == "DELETE":
                table_name = record["table"]
                page_no = record["page_no"]
                slot_id = record["slot_id"]
                
                # Apply deletion physically
                page = self.get_page(table_name, page_no)
                page.delete_record(slot_id, xmax=tx_id)
                self.mark_page_dirty(table_name, page_no)

        # 3. UNDO phase (rollback uncommitted active transactions)
        for record in reversed(log_records):
            tx_id = record["tx_id"]
            if tx_id in active_txs:
                rtype = record["type"]
                table_name = record.get("table")
                page_no = record.get("page_no")
                slot_id = record.get("slot_id")
                
                if rtype == "INSERT":
                    page = self.get_page(table_name, page_no)
                    page.rollback_insert(slot_id)
                    self.mark_page_dirty(table_name, page_no)
                elif rtype == "DELETE":
                    page = self.get_page(table_name, page_no)
                    page.rollback_delete(slot_id)
                    self.mark_page_dirty(table_name, page_no)
                    
        # Write ABORT records for the active transactions that got rolled back
        for tx_id in active_txs:
            self.wal.log_abort(tx_id)
            aborted_txs.add(tx_id)

        # Set transaction manager states
        self.tm.committed_txs = committed_txs
        self.tm.aborted_txs = aborted_txs
        if log_records:
            self.tm.next_tx_id = max(r["tx_id"] for r in log_records) + 1
            
        self.flush_all_dirty()

    def rollback_tx_memory(self, tx_id):
        # Rollback heap changes and in-memory index changes for aborted transaction
        for table_name, page_no, slot_id, op_type, offset, record_dict in reversed(self.tx_ops[tx_id]):
            page = self.get_page(table_name, page_no)
            if op_type == "INSERT":
                page.rollback_insert(slot_id)
                # Remove from B+ tree indexes
                for col in self.catalog["tables"][table_name]["indexes"]:
                    if col in record_dict:
                        self.get_index(table_name, col).delete(record_dict[col])
            elif op_type == "DELETE":
                page.rollback_delete(slot_id)
                # Re-add to B+ tree indexes
                for col in self.catalog["tables"][table_name]["indexes"]:
                    if col in record_dict:
                        self.get_index(table_name, col).insert(record_dict[col], (page_no, slot_id))
                        
            self.mark_page_dirty(table_name, page_no)
        del self.tx_ops[tx_id]

    def execute_statement(self, sql, tx=None):
        parsed = SQLParser.parse(sql)
        
        if tx is not None:
            return self.execute_parsed(parsed, tx)
            
        # Run inside implicit transaction
        tx = self.tm.begin_tx()
        self.wal.log_begin(tx.tx_id)
        try:
            res = self.execute_parsed(parsed, tx)
            self.tm.commit_tx(tx.tx_id)
            self.wal.log_commit(tx.tx_id)
            self.flush_all_dirty()
            return res
        except Exception as e:
            self.tm.abort_tx(tx.tx_id)
            self.wal.log_abort(tx.tx_id)
            self.rollback_tx_memory(tx.tx_id)
            self.flush_all_dirty()
            raise e

    def execute_parsed(self, parsed, tx):
        ttype = parsed["type"]
        
        if ttype == "INSERT":
            table_name = parsed["table"]
            if table_name not in self.catalog["tables"]:
                raise ValueError(f"Table {table_name} does not exist.")
                
            table_info = self.catalog["tables"][table_name]
            schema = table_info["schema"]
            pk = table_info["primary_key"]
            cols = list(schema.keys())
            vals = parsed["values"]
            
            if len(vals) != len(cols):
                raise ValueError(f"Value count mismatch: expected {len(cols)}, got {len(vals)}")
                
            record_dict = {c: v for c, v in zip(cols, vals)}
            pk_val = record_dict[pk]
            
            # Primary Key Unique check
            pk_index = self.get_index(table_name, pk)
            if pk_index:
                existing_rid = pk_index.search(pk_val)
                if existing_rid:
                    # Verify if existing tuple is visible
                    epg, esl = existing_rid
                    page = self.get_page(table_name, epg)
                    rec = page.get_record(esl)
                    if rec:
                        ex_xmin, ex_xmax, _ = rec
                        if self.tm.is_visible(ex_xmin, ex_xmax, tx):
                            raise DuplicateKeyError(f"Duplicate key error: {pk} = {pk_val} already exists.")

            # Serialize
            data_bytes = self.serialize_record(table_name, record_dict)
            
            # Find page with space
            hf = self.get_heap_file(table_name)
            num_pages = hf.num_pages()
            page_no = 0
            slot_id = None
            
            while page_no < num_pages:
                page = self.get_page(table_name, page_no)
                slot_id = page.insert_record(data_bytes, tx.tx_id)
                if slot_id is not None:
                    break
                page_no += 1
                
            if slot_id is None:
                # Need to allocate a new page
                new_page = Page()
                page_no = hf.append_page(new_page)
                # Load page and insert
                page = self.get_page(table_name, page_no)
                slot_id = page.insert_record(data_bytes, tx.tx_id)
                
            # Log insertion
            offset, length = page.get_slot(slot_id)
            self.wal.log_insert(tx.tx_id, table_name, page_no, slot_id, offset, data_bytes)
            self.mark_page_dirty(table_name, page_no)
            
            # Save transaction changes for rollback
            self.tx_ops[tx.tx_id].append((table_name, page_no, slot_id, "INSERT", offset, record_dict))
            
            # Update B+ tree indexes in memory
            for col in table_info["indexes"]:
                if col in record_dict:
                    self.get_index(table_name, col).insert(record_dict[col], (page_no, slot_id))
                    
            return f"Inserted 1 row."

        elif ttype == "DELETE":
            table_name = parsed["table"]
            if table_name not in self.catalog["tables"]:
                raise ValueError(f"Table {table_name} does not exist.")
                
            # 1. Use scan to find matching rows
            table_info = self.catalog["tables"][table_name]
            where_col = parsed["where_col"]
            where_val = parsed["where_val"]
            
            # Select scan type based on optimizer cost
            has_index = (where_col in table_info["indexes"]) if where_col else False
            hf = self.get_heap_file(table_name)
            num_pages = hf.num_pages()
            
            # Estimate cardinality of scan
            if where_col:
                sel = CostBasedOptimizer.estimate_selectivity("=", where_val)
            else:
                sel = 1.0
                
            num_records = 0
            if num_pages > 0:
                # Quick count: table records estimate
                mock_t = Transaction(999999, set())
                sc = TableScanOperator(self, table_name, mock_t)
                sc.open()
                while sc.next():
                    num_records += 1
                sc.close()
                
            scan_decision, est_cost = CostBasedOptimizer.choose_scan_type(num_pages, num_records, sel, has_index)
            
            if scan_decision == "INDEX_SCAN":
                scan_op = IndexScanOperator(self, table_name, where_col, where_val, tx, "=")
            else:
                scan_op = TableScanOperator(self, table_name, tx)
                if where_col:
                    scan_op = FilterOperator(scan_op, where_col, "=", where_val)
                    
            # 2. Iterate and mark deleted
            deleted_count = 0
            scan_op.open()
            rows_to_delete = []
            while True:
                row = scan_op.next()
                if row is None:
                    break
                rows_to_delete.append(row)
            scan_op.close()
            
            for row in rows_to_delete:
                page_no, slot_id = row["_rid"]
                page = self.get_page(table_name, page_no)
                
                # Check write-write conflict
                xmin, xmax, _ = page.get_record(slot_id)
                self.tm.check_write_conflict(xmax, tx)
                
                # Perform deletion
                page.delete_record(slot_id, tx.tx_id)
                self.wal.log_delete(tx.tx_id, table_name, page_no, slot_id)
                self.mark_page_dirty(table_name, page_no)
                
                # Track for rollback
                orig_data = self.deserialize_record(table_name, page.get_record(slot_id)[2])
                self.tx_ops[tx.tx_id].append((table_name, page_no, slot_id, "DELETE", None, orig_data))
                
                # Remove from indexes
                for col in table_info["indexes"]:
                    if col in orig_data:
                        self.get_index(table_name, col).delete(orig_data[col])
                        
                deleted_count += 1
                
            return f"Deleted {deleted_count} rows."

        elif ttype == "SELECT":
            table_name = parsed["table"]
            if table_name not in self.catalog["tables"]:
                raise ValueError(f"Table {table_name} does not exist.")
                
            # Build scan operator
            where_col = parsed["where_col"]
            where_op = parsed["where_op"]
            where_val = parsed["where_val"]
            
            # Selectivity/Cost Decision
            table_info = self.catalog["tables"][table_name]
            has_index = (where_col in table_info["indexes"]) if where_col else False
            hf = self.get_heap_file(table_name)
            num_pages = hf.num_pages()
            
            # Estimate cost
            if where_col:
                sel = CostBasedOptimizer.estimate_selectivity(where_op, where_val)
            else:
                sel = 1.0
                
            # Count records for estimation
            num_records = 0
            if num_pages > 0:
                mock_t = Transaction(999999, set())
                sc = TableScanOperator(self, table_name, mock_t)
                sc.open()
                while sc.next():
                    num_records += 1
                sc.close()
                
            scan_decision, est_cost = CostBasedOptimizer.choose_scan_type(num_pages, num_records, sel, has_index)
            
            # Select Scan Operator
            if scan_decision == "INDEX_SCAN":
                scan_op = IndexScanOperator(self, table_name, where_col, where_val, tx, where_op)
            else:
                scan_op = TableScanOperator(self, table_name, tx)
                if where_col:
                    scan_op = FilterOperator(scan_op, where_col, where_op, where_val)
                    
            # Check for join
            join_table = parsed["join_table"]
            if join_table:
                if join_table not in self.catalog["tables"]:
                    raise ValueError(f"Join table {join_table} does not exist.")
                    
                join_col1 = parsed["join_col1"]
                join_col2 = parsed["join_col2"]
                
                # Check join order using cost optimizer
                # table 1: table_name, table 2: join_table
                t1_info = {
                    "name": table_name,
                    "num_pages": num_pages,
                    "num_records": num_records,
                    "has_index_on_join_col": (join_col1.split(".")[-1] in table_info["indexes"])
                }
                
                join_table_info = self.catalog["tables"][join_table]
                j_hf = self.get_heap_file(join_table)
                j_num_pages = j_hf.num_pages()
                
                # Count inner table records
                j_num_records = 0
                if j_num_pages > 0:
                    mock_t = Transaction(999999, set())
                    j_sc = TableScanOperator(self, join_table, mock_t)
                    j_sc.open()
                    while j_sc.next():
                        j_num_records += 1
                    j_sc.close()
                    
                t2_info = {
                    "name": join_table,
                    "num_pages": j_num_pages,
                    "num_records": j_num_records,
                    "has_index_on_join_col": (join_col2.split(".")[-1] in join_table_info["indexes"])
                }
                
                outer_name, inner_name, join_cost = CostBasedOptimizer.choose_join_order(t1_info, t2_info)
                
                # Let's rebuild operators based on the chosen join order
                # For inner table factory:
                def inner_op_factory():
                    return TableScanOperator(self, inner_name, tx)
                    
                # Setup outer operator
                if outer_name == table_name:
                    outer_op = scan_op
                    join_op = NestedLoopJoinOperator(outer_op, inner_op_factory, join_col1, join_col2)
                else:
                    # outer is join_table
                    # scan outer
                    outer_op = TableScanOperator(self, join_table, tx)
                    join_op = NestedLoopJoinOperator(outer_op, lambda: scan_op, join_col2, join_col1)
                    
                root_op = join_op
            else:
                root_op = scan_op
                
            # Apply project
            root_op = ProjectOperator(root_op, parsed["columns"])
            
            # Execute and gather rows
            results = []
            root_op.open()
            while True:
                r = root_op.next()
                if r is None:
                    break
                # Remove internal metadata columns for final result
                clean_r = {k: v for k, v in r.items() if not k.startswith("_")}
                results.append(clean_r)
            root_op.close()
            
            return results
