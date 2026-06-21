import os
import sys
import json
from typing import List, Dict, Any, Optional

from src.storage.page import Page
from src.storage.heap_file import HeapFile
from src.storage.buffer_pool import BufferPool
from src.indexing.bplus_tree import BPlusTree
from src.parser.sql_parser import SQLParser, tokenize
from src.executor.executor import QueryExecutor
from src.optimizer.optimizer import CostBasedOptimizer
from src.transactions.lock_manager import LockManager, TransactionAbortException
from src.transactions.transaction_manager import TransactionManager
from src.recovery.wal import WALManager
from src.recovery.recovery_manager import RecoveryManager
from src.extension.mvcc import MVCCManager

class MiniDB:
    def __init__(self, data_dir: str = "MiniDB_Projects/Team_QueryCrafters/data", verbose: bool = True, txn_mode: str = "2PL"):
        self.data_dir = data_dir
        os.makedirs(self.data_dir, exist_ok=True)
        self.verbose = verbose
        self.default_txn_mode = txn_mode.upper()
        
        # Core components
        self.lock_manager = LockManager(verbose=self.verbose)
        self.wal_manager = WALManager(self.data_dir)
        self.buffer_pool = BufferPool(capacity=10, wal_manager=self.wal_manager)
        self.transaction_manager = TransactionManager(self.lock_manager, self.wal_manager)
        self.recovery_manager = RecoveryManager(self, self.wal_manager)
        self.transaction_manager.recovery_manager = self.recovery_manager
        self.mvcc_manager = MVCCManager(self.transaction_manager)
        self.optimizer = CostBasedOptimizer(verbose=self.verbose)
        self.executor = QueryExecutor(self)
        
        # Tables & indexes registry
        self.tables: Dict[str, HeapFile] = {}
        self.indexes: Dict[str, Dict[str, BPlusTree]] = {}
        self.schemas: Dict[str, List[dict]] = {}
        
        # Load catalog (persisted schema)
        self.catalog_path = os.path.join(self.data_dir, "catalog.json")
        self.load_catalog()
        
        # Run crash recovery
        if self.verbose:
            print("[Recovery] Initializing recovery analysis...")
        self.recovery_manager.recover()
        if self.verbose:
            print("[Recovery] Recovery complete. Committed transactions restored.")
            
        # Re-populate stats after loading and recovering
        self.rebuild_statistics()

        self.current_txn_id: Optional[int] = None

    def load_catalog(self):
        if os.path.exists(self.catalog_path):
            with open(self.catalog_path, "r") as f:
                try:
                    self.schemas = json.load(f)
                except json.JSONDecodeError:
                    self.schemas = {}
            
            # Reconstruct table and index instances
            for table_name, columns in self.schemas.items():
                self.tables[table_name] = HeapFile(table_name, self.data_dir, self.buffer_pool)
                self.indexes[table_name] = {}
                
                # Load primary and secondary index configurations
                for col in columns:
                    if col["primary_key"]:
                        idx_name = f"{table_name}_{col['name']}.idx"
                        idx_path = os.path.join(self.data_dir, idx_name)
                        self.indexes[table_name][col["name"]] = BPlusTree(idx_path)
                    elif col["name"] in ("age", "name"):
                        # Automatic secondary index columns
                        idx_name = f"{table_name}_{col['name']}.idx"
                        idx_path = os.path.join(self.data_dir, idx_name)
                        self.indexes[table_name][col["name"]] = BPlusTree(idx_path)
                
                self.optimizer.init_table_stats(table_name, columns)

    def save_catalog(self):
        with open(self.catalog_path, "w") as f:
            json.dump(self.schemas, f)

    def create_table(self, table_name: str, columns: List[dict]):
        if table_name in self.schemas:
            raise ValueError(f"Table '{table_name}' already exists.")
            
        self.schemas[table_name] = columns
        self.save_catalog()
        
        # Initialize storage files
        self.tables[table_name] = HeapFile(table_name, self.data_dir, self.buffer_pool)
        self.indexes[table_name] = {}
        
        # Create indexes
        for col in columns:
            if col["primary_key"]:
                idx_name = f"{table_name}_{col['name']}.idx"
                idx_path = os.path.join(self.data_dir, idx_name)
                self.indexes[table_name][col["name"]] = BPlusTree(idx_path)
            elif col["name"] in ("age", "name"):
                idx_name = f"{table_name}_{col['name']}.idx"
                idx_path = os.path.join(self.data_dir, idx_name)
                self.indexes[table_name][col["name"]] = BPlusTree(idx_path)

        # Init optimizer stats
        self.optimizer.init_table_stats(table_name, columns)

    def get_table(self, table_name: str) -> HeapFile:
        if table_name not in self.tables:
            raise ValueError(f"Table '{table_name}' does not exist.")
        return self.tables[table_name]

    def get_primary_key_column(self, table_name: str) -> Optional[str]:
        if table_name not in self.schemas:
            return None
        for col in self.schemas[table_name]:
            if col["primary_key"]:
                return col["name"]
        return None

    def update_indexes_for_insert(self, table_name: str, record_version: dict, record_id: tuple):
        payload = record_version["_data"]
        for col_name, idx in self.indexes.get(table_name, {}).items():
            if col_name in payload:
                idx.insert(payload[col_name], record_id)
        self.optimizer.update_stats_on_insert(table_name, payload)

    def update_indexes_for_delete(self, table_name: str, record_version: dict):
        payload = record_version["_data"]
        for col_name, idx in self.indexes.get(table_name, {}).items():
            if col_name in payload:
                idx.delete(payload[col_name])
        self.optimizer.update_stats_on_delete(table_name, payload)

    def rebuild_statistics(self):
        """Scans all tables to rebuild optimizer statistics on startup."""
        for table_name, columns in self.schemas.items():
            self.optimizer.init_table_stats(table_name, columns)
            heap_file = self.tables[table_name]
            
            # Simple serial scan to rebuild stats
            scanner = heap_file.scan_all()
            for rid, raw_bytes in scanner:
                page_id, slot_id = rid
                try:
                    record_version = json.loads(raw_bytes.decode("utf-8"))
                    # Visibly check with empty txn context (all committed data is visible)
                    if self.mvcc_manager.is_version_visible(record_version, 0):
                        payload = record_version["_data"]
                        self.optimizer.update_stats_on_insert(table_name, payload)
                except (json.JSONDecodeError, UnicodeDecodeError):
                    pass

    def execute(self, sql: str) -> List[Dict[str, Any]]:
        return self.execute_sql(sql)

    def recover(self):
        return self.recovery_manager.recover()

    def force_save_all_indexes(self):
        """Forces all in-memory indexes to write their status to disk."""
        for table_name, idx_dict in self.indexes.items():
            for col_name, idx in idx_dict.items():
                idx.save(force=True)

    def execute_sql(self, sql: str) -> List[Dict[str, Any]]:
        # Tokenize briefly to check if it's transaction control
        tokens = tokenize(sql)
        if not tokens:
            return []
            
        cmd_upper = tokens[0].upper()

        # Handle SET TRANSACTION MODE command
        if cmd_upper == "SET" and len(tokens) >= 4 and tokens[1].upper() == "TRANSACTION" and tokens[2].upper() == "MODE":
            mode = tokens[3].upper()
            if mode in ("2PL", "MVCC"):
                self.default_txn_mode = mode
                return [{"status": f"Default transaction mode set to {mode}."}]
            else:
                raise ValueError("Mode must be '2PL' or 'MVCC'")

        # Parse sql into AST
        parser = SQLParser(sql)
        ast = parser.parse()

        # Handle BEGIN
        if ast["type"] == "BeginTxn":
            if self.current_txn_id is not None:
                raise ValueError("Already inside a transaction.")
            self.current_txn_id = self.transaction_manager.begin_transaction(mode=self.default_txn_mode)
            return [{"status": f"Transaction {self.current_txn_id} started. Mode: {self.default_txn_mode}"}]

        # Handle COMMIT
        if ast["type"] == "CommitTxn":
            if self.current_txn_id is None:
                raise ValueError("No active transaction to commit.")
            txn_id = self.current_txn_id
            self.transaction_manager.commit(txn_id)
            self.current_txn_id = None
            self.force_save_all_indexes()
            return [{"status": f"Transaction {txn_id} committed. Locks released."}]

        # Handle ROLLBACK
        if ast["type"] == "RollbackTxn":
            if self.current_txn_id is None:
                raise ValueError("No active transaction to rollback.")
            txn_id = self.current_txn_id
            self.transaction_manager.rollback(txn_id)
            self.current_txn_id = None
            self.force_save_all_indexes()
            return [{"status": f"Transaction {txn_id} rolled back. Locks released."}]

        # Normal queries (autocommit if not in a transaction)
        autocommit = (self.current_txn_id is None)
        if autocommit:
            txn_id = self.transaction_manager.begin_transaction(mode=self.default_txn_mode)
        else:
            txn_id = self.current_txn_id

        try:
            results = self.executor.execute(ast, txn_id)
            if autocommit:
                self.transaction_manager.commit(txn_id)
                self.force_save_all_indexes()
            return results
        except Exception as e:
            if autocommit:
                try:
                    self.transaction_manager.rollback(txn_id)
                except Exception:
                    pass
            elif isinstance(e, TransactionAbortException):
                # If transaction was aborted internally (deadlock or conflict), clean current txn session
                self.current_txn_id = None
            raise e

def print_table(rows: List[dict]):
    if not rows:
        print("Empty set (0 rows)")
        return
        
    # Check if this is a schema result or status string
    if "status" in rows[0] and len(rows[0]) == 1:
        print(rows[0]["status"])
        return
        
    if "rows_inserted" in rows[0] and len(rows[0]) == 1:
        print(f"Inserted {rows[0]['rows_inserted']} row.")
        return
        
    if "rows_deleted" in rows[0] and len(rows[0]) == 1:
        print(f"Deleted {rows[0]['rows_deleted']} row.")
        return

    # Normal rows formatting: filter out metadata columns
    keys = [k for k in rows[0].keys() if not k.startswith("_")]
    if not keys:
        print("Empty set (0 rows)")
        return

    # Calculate column widths
    widths = {k: len(str(k)) for k in keys}
    for row in rows:
        for k in keys:
            widths[k] = max(widths[k], len(str(row.get(k, ""))))

    # Draw table
    header_line = "| " + " | ".join(f"{str(k):<{widths[k]}}" for k in keys) + " |"
    sep_line = "|-" + "-+-".join("-" * widths[k] for k in keys) + "-|"
    print(header_line)
    print(sep_line)
    for row in rows:
        line = "| " + " | ".join(f"{str(row.get(k, '')):<{widths[k]}}" for k in keys) + " |"
        print(line)
    print(f"({len(rows)} rows)")

def start_repl():
    # Setup working directories under project workspace
    db = MiniDB(verbose=True)
    print("\n" + "=" * 50)
    print("Welcome to MiniDB Relational Database Engine!")
    print("Press Ctrl+C or Ctrl+D or type 'exit' to quit.")
    print("Use: SET TRANSACTION MODE MVCC; to switch modes.")
    print("=" * 50 + "\n")

    while True:
        try:
            prompt = "MiniDB> " if db.current_txn_id is None else f"MiniDB [Txn {db.current_txn_id}]> "
            sql_line = input(prompt)
            if not sql_line.strip():
                continue
            if sql_line.strip().lower() in ("exit", "quit"):
                break
                
            results = db.execute_sql(sql_line)
            print_table(results)
            
        except KeyboardInterrupt:
            print("\nAborted query.")
            continue
        except EOFError:
            print("\nExiting.")
            break
        except Exception as e:
            print(f"Error: {e}")

if __name__ == "__main__":
    start_repl()
