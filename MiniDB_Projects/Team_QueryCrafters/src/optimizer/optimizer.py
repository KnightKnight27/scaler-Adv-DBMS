import math
from typing import Dict, Any, Tuple, List
from src.executor.operators import Operator, SeqScan, IndexScan, Filter, Projection, NestedLoopJoin

class CostBasedOptimizer:
    def __init__(self, verbose: bool = True):
        # Maps table_name -> stats dict:
        # { "row_count": int, "columns": { col_name: { "min": val, "max": val, "distinct": set } } }
        self.stats = {}
        self.verbose = verbose

    def log(self, message: str):
        if self.verbose:
            print(f"[Optimizer] {message}")

    def init_table_stats(self, table_name: str, columns: List[dict]):
        if table_name not in self.stats:
            self.stats[table_name] = {
                "row_count": 0,
                "columns": {col["name"]: {"min": None, "max": None, "distinct": set()} for col in columns}
            }

    def update_stats_on_insert(self, table_name: str, record_dict: Dict[str, Any]):
        if table_name not in self.stats:
            return
        
        table_stats = self.stats[table_name]
        table_stats["row_count"] += 1
        
        for col_name, val in record_dict.items():
            if col_name in table_stats["columns"] and val is not None:
                col_stats = table_stats["columns"][col_name]
                col_stats["distinct"].add(val)
                
                # Update min/max
                if col_stats["min"] is None or val < col_stats["min"]:
                    col_stats["min"] = val
                if col_stats["max"] is None or val > col_stats["max"]:
                    col_stats["max"] = val

    def update_stats_on_delete(self, table_name: str, record_dict: Dict[str, Any]):
        if table_name not in self.stats:
            return
        
        table_stats = self.stats[table_name]
        table_stats["row_count"] = max(0, table_stats["row_count"] - 1)
        
        # Decremental distinct adjustments: we can keep them or remove the value from distinct
        # for a simple DBMS, removing the value from the set is fine
        for col_name, val in record_dict.items():
            if col_name in table_stats["columns"] and val is not None:
                col_stats = table_stats["columns"][col_name]
                # Note: other records might have the same value, so removing it directly
                # could underestimate distinct values, but for a simple capstone it's fine.
                # To be safer, we can just keep distinct values or remove if we are sure it's 0 rows left.
                if table_stats["row_count"] == 0:
                    col_stats["distinct"].clear()
                    col_stats["min"] = None
                    col_stats["max"] = None
                else:
                    if val in col_stats["distinct"]:
                        # Just keep it to avoid scanning unless table goes empty.
                        pass

    def get_selectivity(self, table_name: str, col_name: str, op: str, value: Any) -> float:
        if table_name not in self.stats:
            return 0.1
        
        table_stats = self.stats[table_name]
        if col_name not in table_stats["columns"]:
            return 0.1
            
        col_stats = table_stats["columns"][col_name]
        num_distinct = len(col_stats["distinct"])
        if num_distinct == 0:
            return 1.0

        min_val = col_stats["min"]
        max_val = col_stats["max"]

        if op == "=":
            return 1.0 / num_distinct
            
        elif op in (">", ">="):
            if min_val is None or max_val is None or max_val == min_val:
                return 0.5
            try:
                sel = (max_val - value) / (max_val - min_val)
                return max(0.0, min(1.0, sel))
            except Exception:
                return 0.5
                
        elif op in ("<", "<="):
            if min_val is None or max_val is None or max_val == min_val:
                return 0.5
            try:
                sel = (value - min_val) / (max_val - min_val)
                return max(0.0, min(1.0, sel))
            except Exception:
                return 0.5
                
        elif op == "!=":
            return 1.0 - (1.0 / num_distinct)
            
        return 1.0

    def estimate_scan_cost(self, table_name: str, col_name: str, op: str, value: Any, db) -> Tuple[str, float]:
        """
        SeqScan cost = num_pages
        IndexScan cost = log2(num_rows) + selectivity * num_rows
        """
        heap_file = db.get_table(table_name)
        num_pages = max(1, heap_file.get_num_pages())
        
        table_stats = self.stats.get(table_name, {"row_count": 0})
        num_rows = table_stats["row_count"]
        
        seq_cost = float(num_pages)
        
        # Check if index exists on the predicate column
        index_exists = col_name in db.indexes.get(table_name, {})
        if not index_exists or op == "!=":
            return "SeqScan", seq_cost
            
        selectivity = self.get_selectivity(table_name, col_name, op, value)
        log_rows = math.log2(max(1, num_rows))
        index_cost = log_rows + selectivity * num_rows
        
        if index_cost < seq_cost:
            return "IndexScan", index_cost
        else:
            return "SeqScan", seq_cost

    def optimize_select(self, ast: dict, db, txn_id: int) -> Operator:
        """
        Builds the optimized physical plan for SELECT statements.
        Handles SeqScan vs IndexScan selection and Join order selection.
        """
        from_table = ast["from_table"]
        from_alias = ast["from_alias"]
        join = ast.get("join")
        where = ast.get("where")
        columns = ast["columns"]

        # Parse WHERE condition if applicable
        where_col, where_op, where_val = None, None, None
        if where:
            where_col = where["left"]
            where_op = where["op"]
            where_val = where["right"]
            # Remove table prefix from where column for checking stats/index
            where_col_bare = where_col.split(".")[-1]

        # Case 1: No Join (Single table)
        if not join:
            chosen_scan = "SeqScan"
            cost = max(1, db.get_table(from_table).get_num_pages())
            
            if where:
                chosen_scan, cost = self.estimate_scan_cost(from_table, where_col_bare, where_op, where_val, db)
                
            self.log(f"{chosen_scan} chosen on {from_table}, cost={cost:.2f}")

            # Build scan operator
            if chosen_scan == "IndexScan":
                index = db.indexes[from_table][where_col_bare]
                scan = IndexScan(from_table, db.get_table(from_table), db, txn_id, from_alias, index, where_col_bare, where_op, where_val)
            else:
                scan = SeqScan(from_table, db.get_table(from_table), db, txn_id, from_alias)

            # Build filter if SeqScan (since IndexScan does it, but we can still filter for other ops)
            curr = scan
            if where and chosen_scan == "SeqScan":
                # Build predicate
                def pred(row):
                    val = row.get(where_col)
                    if val is None:
                        val = row.get(where_col_bare)
                    if val is None:
                        return False
                    
                    if where_op == "=":
                        return val == where_val
                    elif where_op == ">":
                        return val > where_val
                    elif where_op == "<":
                        return val < where_val
                    elif where_op == ">=":
                        return val >= where_val
                    elif where_op == "<=":
                        return val <= where_val
                    elif where_op == "!=":
                        return val != where_val
                    return False
                curr = Filter(curr, pred)

            # Build projection
            plan = Projection(curr, columns)
            return plan

        # Case 2: 2-Table Join
        else:
            join_table = join["table"]
            join_alias = join["alias"]
            join_left = join["on_left"]
            join_right = join["on_right"]

            # Evaluate join order 1: (from_table) JOIN (join_table)
            # Evaluate join order 2: (join_table) JOIN (from_table)
            
            # Scan costs for both tables
            # (Note: we check if the WHERE clause applies to from_table or join_table)
            where_table = None
            if where:
                # Deduce which table the where clause belongs to
                col_prefix = where_col.split(".")[0] if "." in where_col else None
                if col_prefix:
                    if col_prefix == from_alias or col_prefix == from_table:
                        where_table = from_table
                    elif col_prefix == join_alias or col_prefix == join_table:
                        where_table = join_table
                else:
                    # check schemas
                    if where_col_bare in [c["name"] for c in db.schemas[from_table]]:
                        where_table = from_table
                    else:
                        where_table = join_table

            # Cost A
            cost_A_scan = max(1, db.get_table(from_table).get_num_pages())
            A_scan_type = "SeqScan"
            if where and where_table == from_table:
                A_scan_type, cost_A_scan = self.estimate_scan_cost(from_table, where_col_bare, where_op, where_val, db)
            
            rows_A = self.stats.get(from_table, {}).get("row_count", 0)
            if where and where_table == from_table:
                rows_A *= self.get_selectivity(from_table, where_col_bare, where_op, where_val)
            rows_A = max(1, rows_A)

            # Cost B
            cost_B_scan = max(1, db.get_table(join_table).get_num_pages())
            B_scan_type = "SeqScan"
            if where and where_table == join_table:
                B_scan_type, cost_B_scan = self.estimate_scan_cost(join_table, where_col_bare, where_op, where_val, db)

            rows_B = self.stats.get(join_table, {}).get("row_count", 0)
            if where and where_table == join_table:
                rows_B *= self.get_selectivity(join_table, where_col_bare, where_op, where_val)
            rows_B = max(1, rows_B)

            # Cost calculation for Order 1: A JOIN B
            # Total cost = cost(A) + rows(A) * cost(SeqScan B)
            cost_order_1 = cost_A_scan + rows_A * max(1, db.get_table(join_table).get_num_pages())

            # Cost calculation for Order 2: B JOIN A
            # Total cost = cost(B) + rows(B) * cost(SeqScan A)
            cost_order_2 = cost_B_scan + rows_B * max(1, db.get_table(from_table).get_num_pages())

            # Choose join order
            if cost_order_1 <= cost_order_2:
                self.log(f"Join order chosen: {from_table} JOIN {join_table}, cost={cost_order_1:.2f} (vs {cost_order_2:.2f})")
                left_table, left_alias, left_scan_type, left_cost_scan = from_table, from_alias, A_scan_type, cost_A_scan
                right_table, right_alias, right_scan_type, right_cost_scan = join_table, join_alias, B_scan_type, cost_B_scan
                left_join_key, right_join_key = join_left, join_right
            else:
                self.log(f"Join order chosen: {join_table} JOIN {from_table}, cost={cost_order_2:.2f} (vs {cost_order_1:.2f})")
                left_table, left_alias, left_scan_type, left_cost_scan = join_table, join_alias, B_scan_type, cost_B_scan
                right_table, right_alias, right_scan_type, right_cost_scan = from_table, from_alias, A_scan_type, cost_A_scan
                left_join_key, right_join_key = join_right, join_left

            # Build left operator
            if left_scan_type == "IndexScan" and where and where_table == left_table:
                index = db.indexes[left_table][where_col_bare]
                left_op = IndexScan(left_table, db.get_table(left_table), db, txn_id, left_alias, index, where_col_bare, where_op, where_val)
            else:
                left_op = SeqScan(left_table, db.get_table(left_table), db, txn_id, left_alias)
                if where and where_table == left_table:
                    def pred(row):
                        val = row.get(where_col)
                        if val is None:
                            val = row.get(where_col_bare)
                        if val is None:
                            return False
                        
                        if where_op == "=":
                            return val == where_val
                        elif where_op == ">":
                            return val > where_val
                        elif where_op == "<":
                            return val < where_val
                        elif where_op == ">=":
                            return val >= where_val
                        elif where_op == "<=":
                            return val <= where_val
                        elif where_op == "!=":
                            return val != where_val
                        return False
                    left_op = Filter(left_op, pred)

            # Build right operator
            if right_scan_type == "IndexScan" and where and where_table == right_table:
                index = db.indexes[right_table][where_col_bare]
                right_op = IndexScan(right_table, db.get_table(right_table), db, txn_id, right_alias, index, where_col_bare, where_op, where_val)
            else:
                right_op = SeqScan(right_table, db.get_table(right_table), db, txn_id, right_alias)
                if where and where_table == right_table:
                    def pred(row):
                        val = row.get(where_col)
                        if val is None:
                            val = row.get(where_col_bare)
                        if val is None:
                            return False
                        
                        if where_op == "=":
                            return val == where_val
                        elif where_op == ">":
                            return val > where_val
                        elif where_op == "<":
                            return val < where_val
                        elif where_op == ">=":
                            return val >= where_val
                        elif where_op == "<=":
                            return val <= where_val
                        elif where_op == "!=":
                            return val != where_val
                        return False
                    right_op = Filter(right_op, pred)

            # Build NestLoopJoin
            join_op = NestedLoopJoin(left_op, right_op, left_join_key, right_join_key)
            plan = Projection(join_op, columns)
            return plan

class Optimizer:
    def __init__(self):
        self.indexes = {}
        self.stats = {}

    def register_index(self, table_name: str, col_name: str, tree):
        if table_name not in self.indexes:
            self.indexes[table_name] = {}
        self.indexes[table_name][col_name] = tree

    def update_stats(self, table_name: str, row_count: int, num_pages: int):
        self.stats[table_name] = {
            "row_count": row_count,
            "num_pages": num_pages
        }

    def choose_plan(self, table_name: str, predicate_col: str, predicate_val: Any) -> dict:
        has_index = predicate_col in self.indexes.get(table_name, {})
        row_count = self.stats.get(table_name, {}).get("row_count", 0)
        if has_index and row_count > 50:
            return {"type": "IndexScan"}
        else:
            return {"type": "SeqScan"}
