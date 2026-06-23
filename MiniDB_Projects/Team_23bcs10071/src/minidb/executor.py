class Operator:
    def open(self):
        pass

    def next(self):
        # Returns next tuple or None if exhausted
        return None

    def close(self):
        pass

    def __iter__(self):
        self.open()
        try:
            while True:
                t = self.next()
                if t is None:
                    break
                yield t
        finally:
            self.close()


class TableScanOperator(Operator):
    def __init__(self, db, table_name, tx):
        self.db = db
        self.table_name = table_name
        self.tx = tx
        self.heap_file = db.get_heap_file(table_name)
        self.curr_page_no = 0
        self.curr_slot_id = 0
        self.num_pages = 0

    def open(self):
        self.curr_page_no = 0
        self.curr_slot_id = 0
        self.num_pages = self.heap_file.num_pages()

    def next(self):
        while self.curr_page_no < self.num_pages:
            page = self.db.get_page(self.table_name, self.curr_page_no)
            num_slots = page.get_num_slots()
            
            while self.curr_slot_id < num_slots:
                slot_id = self.curr_slot_id
                self.curr_slot_id += 1
                
                record = page.get_record(slot_id)
                if record is not None:
                    xmin, xmax, payload = record
                    # Check MVCC visibility
                    if self.db.tm.is_visible(xmin, xmax, self.tx):
                        # Deserialize tuple
                        data = self.db.deserialize_record(self.table_name, payload)
                        # Add metadata for RID (helpful for updates/deletes) and table qualification
                        row = {}
                        for k, v in data.items():
                            row[k] = v
                            row[f"{self.table_name}.{k}"] = v
                        row["_rid"] = (self.curr_page_no, slot_id)
                        return row
            
            self.curr_page_no += 1
            self.curr_slot_id = 0
        return None


class IndexScanOperator(Operator):
    def __init__(self, db, table_name, index_name, val, tx, op="="):
        self.db = db
        self.table_name = table_name
        self.index_name = index_name
        self.val = val
        self.tx = tx
        self.op = op
        self.rids = []
        self.curr_idx = 0

    def open(self):
        index = self.db.get_index(self.table_name, self.index_name)
        if self.op == "=":
            rid = index.search(self.val)
            self.rids = [rid] if rid else []
        elif self.op == ">":
            self.rids = [rid for k, rid in index.scan(low_key=self.val + 1)]
        elif self.op == "<":
            self.rids = [rid for k, rid in index.scan(high_key=self.val - 1)]
        elif self.op == ">=":
            self.rids = [rid for k, rid in index.scan(low_key=self.val)]
        elif self.op == "<=":
            self.rids = [rid for k, rid in index.scan(high_key=self.val)]
        self.curr_idx = 0

    def next(self):
        while self.curr_idx < len(self.rids):
            page_no, slot_id = self.rids[self.curr_idx]
            self.curr_idx += 1
            
            page = self.db.get_page(self.table_name, page_no)
            record = page.get_record(slot_id)
            if record is not None:
                xmin, xmax, payload = record
                if self.db.tm.is_visible(xmin, xmax, self.tx):
                    data = self.db.deserialize_record(self.table_name, payload)
                    row = {}
                    for k, v in data.items():
                        row[k] = v
                        row[f"{self.table_name}.{k}"] = v
                    row["_rid"] = (page_no, slot_id)
                    return row
        return None


class FilterOperator(Operator):
    def __init__(self, child, where_col, where_op, where_val):
        self.child = child
        self.where_col = where_col
        self.where_op = where_op
        self.where_val = where_val

    def open(self):
        self.child.open()

    def next(self):
        while True:
            row = self.child.next()
            if row is None:
                return None
            
            if self.where_col not in row:
                # Column doesn't exist, skip or treat as false
                continue
                
            val = row[self.where_col]
            match = False
            if self.where_op == "=":
                match = (val == self.where_val)
            elif self.where_op == ">":
                match = (val > self.where_val)
            elif self.where_op == "<":
                match = (val < self.where_val)
            elif self.where_op == ">=":
                match = (val >= self.where_val)
            elif self.where_op == "<=":
                match = (val <= self.where_val)
            else:
                match = True
                
            if match:
                return row


class NestedLoopJoinOperator(Operator):
    def __init__(self, outer, inner_factory, join_col_outer, join_col_inner):
        """
        inner_factory is a callable that returns an Operator for the inner relation.
        We instantiate it for each outer row.
        """
        self.outer = outer
        self.inner_factory = inner_factory
        self.join_col_outer = join_col_outer
        self.join_col_inner = join_col_inner
        
        self.curr_outer_row = None
        self.curr_inner_op = None

    def open(self):
        self.outer.open()
        self.curr_outer_row = self.outer.next()
        if self.curr_outer_row:
            self.curr_inner_op = self.inner_factory()
            self.curr_inner_op.open()

    def next(self):
        while self.curr_outer_row is not None:
            inner_row = self.curr_inner_op.next()
            if inner_row is None:
                # Outer row finished scanning inner table, move to next outer row
                self.curr_inner_op.close()
                self.curr_outer_row = self.outer.next()
                if self.curr_outer_row is None:
                    return None
                self.curr_inner_op = self.inner_factory()
                self.curr_inner_op.open()
                continue
            
            # Check join condition
            val_outer = self.curr_outer_row.get(self.join_col_outer)
            val_inner = inner_row.get(self.join_col_inner)
            
            if val_outer is not None and val_inner is not None and val_outer == val_inner:
                # Merge row
                merged = {}
                merged.update(self.curr_outer_row)
                merged.update(inner_row)
                return merged
        return None

    def close(self):
        self.outer.close()
        if self.curr_inner_op:
            self.curr_inner_op.close()


class ProjectOperator(Operator):
    def __init__(self, child, columns):
        self.child = child
        self.columns = columns

    def open(self):
        self.child.open()

    def next(self):
        row = self.child.next()
        if row is None:
            return None
        if self.columns == ["*"]:
            return row
        
        projected = {}
        for col in self.columns:
            # We try both qualified and unqualified column names
            if col in row:
                projected[col] = row[col]
            else:
                # Look for matching column base name
                found = False
                for k, v in row.items():
                    if k.split(".")[-1] == col:
                        projected[col] = v
                        found = True
                        break
                if not found:
                    projected[col] = None
        return projected

    def close(self):
        self.child.close()
