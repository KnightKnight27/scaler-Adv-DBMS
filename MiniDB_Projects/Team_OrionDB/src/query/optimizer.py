class TableStats:
    def __init__(self, num_rows, num_pages, column_stats=None):
        self.num_rows = num_rows
        self.num_pages = num_pages
        # column_stats: dict of {col_name: {min_val, max_val, num_distinct}}
        self.column_stats = column_stats if column_stats else {}

class Optimizer:
    PAGE_READ_COST = 10.0
    TUPLE_PROCESS_COST = 1.0

    def __init__(self, catalog=None):
        self.catalog = catalog
        self.stats = {}  # table_name -> TableStats

    def set_stats(self, table_name, num_rows, num_pages, col_stats=None):
        self.stats[table_name] = TableStats(num_rows, num_pages, col_stats)

    def get_stats(self, table_name):
        if table_name in self.stats:
            return self.stats[table_name]
        # Return default stats if none registered
        return TableStats(100, 5, {})

    def estimate_selectivity(self, table_name, condition):
        # condition: (col, op, val)
        if not condition:
            return 1.0
            
        col, op, val = condition
        stats = self.get_stats(table_name)
        
        # Remove table prefix if present (e.g. t1.id -> id)
        col_clean = col.split('.')[-1]
        
        if col_clean not in stats.column_stats:
            # Return standard default selectivities if no column stats
            if op == '=':
                return 0.1
            elif op in ('>', '<', '>=', '<='):
                return 0.33
            else:
                return 1.0

        col_meta = stats.column_stats[col_clean]
        min_v = col_meta.get('min_val')
        max_v = col_meta.get('max_val')
        distinct = col_meta.get('num_distinct', 10)

        if op == '=':
            return 1.0 / max(distinct, 1)
        elif op in ('>', '>='):
            if min_v is not None and max_v is not None and max_v > min_v:
                try:
                    val_num = float(val)
                    if val_num < min_v:
                        return 1.0
                    if val_num > max_v:
                        return 0.0
                    return (max_v - val_num) / (max_v - min_v)
                except ValueError:
                    pass
            return 0.33
        elif op in ('<', '<='):
            if min_v is not None and max_v is not None and max_v > min_v:
                try:
                    val_num = float(val)
                    if val_num < min_v:
                        return 0.0
                    if val_num > max_v:
                        return 1.0
                    return (val_num - min_v) / (max_v - min_v)
                except ValueError:
                    pass
            return 0.33
        elif op == '!=':
            return 1.0 - (1.0 / max(distinct, 1))

        return 1.0

    def choose_access_path(self, table_name, condition, pk_col, b_tree_height=2):
        stats = self.get_stats(table_name)
        
        # SeqScan Cost: read all pages + process all tuples
        seq_scan_cost = (stats.num_pages * self.PAGE_READ_COST) + (stats.num_rows * self.TUPLE_PROCESS_COST)
        
        # IndexScan: only applicable if we filter on the Primary Key with '=' operator
        can_use_index = False
        if condition:
            col, op, val = condition
            col_clean = col.split('.')[-1]
            if col_clean == pk_col and op == '=':
                can_use_index = True

        if not can_use_index:
            return "SeqScan", seq_scan_cost, seq_scan_cost

        # IndexScan Cost: read index nodes + read 1 leaf page + process 1 tuple
        index_scan_cost = ((b_tree_height + 1) * self.PAGE_READ_COST) + (1 * self.TUPLE_PROCESS_COST)
        
        # Select the minimum
        if index_scan_cost < seq_scan_cost:
            return "IndexScan", index_scan_cost, seq_scan_cost
        else:
            return "SeqScan", seq_scan_cost, seq_scan_cost

    def choose_join_order(self, table_a, table_b, join_cond, pk_a, pk_b, b_tree_height_a=2, b_tree_height_b=2):
        # join_cond: (col_a, col_b)
        col_a, col_b = join_cond
        col_a_clean = col_a.split('.')[-1]
        col_b_clean = col_b.split('.')[-1]

        stats_a = self.get_stats(table_a)
        stats_b = self.get_stats(table_b)

        # Helper to compute Scan Cost for a table
        def get_scan_cost(table_name, is_index_lookup, b_tree_height):
            t_stats = self.get_stats(table_name)
            if is_index_lookup:
                # Point lookup cost on index
                return ((b_tree_height + 1) * self.PAGE_READ_COST) + (1 * self.TUPLE_PROCESS_COST)
            else:
                # Seq scan cost
                return (t_stats.num_pages * self.PAGE_READ_COST) + (t_stats.num_rows * self.TUPLE_PROCESS_COST)

        # 1. Option 1: Table A is Outer, Table B is Inner
        # If Table B is scanned using an index lookup (i.e. col_b_clean is pk_b), we use Index Nested Loop Join.
        b_has_index = (col_b_clean == pk_b)
        scan_a_cost = get_scan_cost(table_a, False, b_tree_height_a)
        inner_b_cost = get_scan_cost(table_b, b_has_index, b_tree_height_b)
        cost_a_join_b = scan_a_cost + stats_a.num_rows * inner_b_cost

        # 2. Option 2: Table B is Outer, Table A is Inner
        # If Table A is scanned using index (col_a_clean is pk_a), we use Index Nested Loop Join.
        a_has_index = (col_a_clean == pk_a)
        scan_b_cost = get_scan_cost(table_b, False, b_tree_height_b)
        inner_a_cost = get_scan_cost(table_a, a_has_index, b_tree_height_a)
        cost_b_join_a = scan_b_cost + stats_b.num_rows * inner_a_cost

        # Return results
        if cost_a_join_b <= cost_b_join_a:
            return {
                "order": (table_a, table_b),
                "cost": cost_a_join_b,
                "outer_scan": "SeqScan",
                "inner_scan": "IndexScan" if b_has_index else "SeqScan",
                "costs": (cost_a_join_b, cost_b_join_a)
            }
        else:
            return {
                "order": (table_b, table_a),
                "cost": cost_b_join_a,
                "outer_scan": "SeqScan",
                "inner_scan": "IndexScan" if a_has_index else "SeqScan",
                "costs": (cost_a_join_b, cost_b_join_a)
            }
