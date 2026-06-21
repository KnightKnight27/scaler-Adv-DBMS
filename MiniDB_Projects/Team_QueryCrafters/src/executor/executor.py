from typing import List, Dict, Any
from src.executor.operators import Operator, Insert, Delete, SeqScan, Filter, Projection
from src.transactions.lock_manager import TransactionAbortException

class QueryExecutor:
    def __init__(self, db):
        self.db = db

    def execute(self, ast: Dict[str, Any], txn_id: int) -> List[Dict[str, Any]]:
        ast_type = ast["type"]
        
        if ast_type == "CreateTable":
            self.db.create_table(ast["table_name"], ast["columns"])
            return [{"status": "Table created."}]
            
        elif ast_type == "Insert":
            heap_file = self.db.get_table(ast["table_name"])
            op = Insert(ast["table_name"], heap_file, self.db, txn_id, ast["values"])
            op.open()
            res = op.next()
            op.close()
            return [res] if res else []
            
        elif ast_type == "Delete":
            heap_file = self.db.get_table(ast["table_name"])
            # Build predicate function from where clause
            where = ast.get("where")
            predicate_fn = None
            if where:
                left_col = where["left"]
                op_symbol = where["op"]
                right_val = where["right"]
                
                def pred(row):
                    # Find value in row, supporting fully qualified names or bare names
                    val = row.get(left_col)
                    if val is None:
                        bare_col = left_col.split(".")[-1]
                        val = row.get(bare_col)
                    if val is None:
                        return False
                    
                    if op_symbol == "=":
                        return val == right_val
                    elif op_symbol == ">":
                        return val > right_val
                    elif op_symbol == "<":
                        return val < right_val
                    elif op_symbol == ">=":
                        return val >= right_val
                    elif op_symbol == "<=":
                        return val <= right_val
                    elif op_symbol == "!=":
                        return val != right_val
                    return False
                predicate_fn = pred
                
            op = Delete(ast["table_name"], heap_file, self.db, txn_id, predicate_fn)
            op.open()
            res = op.next()
            op.close()
            return [res] if res else []
            
        elif ast_type == "Select":
            # Delegate select optimization and operator tree building to the optimizer
            plan = self.db.optimizer.optimize_select(ast, self.db, txn_id)
            plan.open()
            results = []
            while True:
                row = plan.next()
                if row is None:
                    break
                results.append(row)
            plan.close()
            return results
            
        elif ast_type in ("BeginTxn", "CommitTxn", "RollbackTxn"):
            # These are handled directly in the database REPL interface / txn API
            return [{"status": f"Transaction command {ast_type} processed."}]
            
        else:
            raise ValueError(f"Unknown AST node type: {ast_type}")
