class CostBasedOptimizer:
    PAGE_READ_COST = 1.0
    TUPLE_PROCESS_COST = 0.1

    @staticmethod
    def estimate_selectivity(op, value=None):
        if op == "=":
            return 0.1
        elif op in (">", "<", ">=", "<="):
            return 0.3
        return 1.0

    @classmethod
    def choose_scan_type(cls, num_pages, num_records, selectivity, has_index):
        if not has_index:
            return "TABLE_SCAN", cls.estimate_table_scan_cost(num_pages, num_records)

        table_scan_cost = cls.estimate_table_scan_cost(num_pages, num_records)
        index_scan_cost = cls.estimate_index_scan_cost(selectivity, num_records)

        if index_scan_cost < table_scan_cost:
            return "INDEX_SCAN", index_scan_cost
        else:
            return "TABLE_SCAN", table_scan_cost

    @classmethod
    def estimate_table_scan_cost(cls, num_pages, num_records):
        # Reads all pages, processes all tuples
        return num_pages * cls.PAGE_READ_COST + num_records * cls.TUPLE_PROCESS_COST

    @classmethod
    def estimate_index_scan_cost(cls, selectivity, num_records):
        # Height of B+ Tree lookup (assume 3 for estimation)
        tree_height = 3.0
        lookup_cost = tree_height * cls.PAGE_READ_COST
        
        # Read the matching data pages
        matching_tuples = num_records * selectivity
        data_read_cost = matching_tuples * cls.PAGE_READ_COST
        cpu_cost = matching_tuples * cls.TUPLE_PROCESS_COST
        
        return lookup_cost + data_read_cost + cpu_cost

    @classmethod
    def choose_join_order(cls, table_a_info, table_b_info):
        """
        table_info is a dict: {"name": str, "num_pages": int, "num_records": int, "has_index_on_join_col": bool}
        We evaluate A JOIN B (A outer, B inner) vs B JOIN A (B outer, A inner).
        We return (outer_table_name, inner_table_name, estimated_cost).
        """
        # Option 1: A outer, B inner
        # Cost(A) + Card(A) * Cost(B)
        cost_a = cls.estimate_table_scan_cost(table_a_info["num_pages"], table_a_info["num_records"])
        if table_b_info["has_index_on_join_col"]:
            # Inner is indexed lookup (selectivity for join key match is 1/Card(B))
            sel = 1.0 / max(1, table_b_info["num_records"])
            cost_inner_b = cls.estimate_index_scan_cost(sel, table_b_info["num_records"])
        else:
            cost_inner_b = cls.estimate_table_scan_cost(table_b_info["num_pages"], table_b_info["num_records"])
        cost_option_1 = cost_a + table_a_info["num_records"] * cost_inner_b

        # Option 2: B outer, A inner
        cost_b = cls.estimate_table_scan_cost(table_b_info["num_pages"], table_b_info["num_records"])
        if table_a_info["has_index_on_join_col"]:
            sel = 1.0 / max(1, table_a_info["num_records"])
            cost_inner_a = cls.estimate_index_scan_cost(sel, table_a_info["num_records"])
        else:
            cost_inner_a = cls.estimate_table_scan_cost(table_a_info["num_pages"], table_a_info["num_records"])
        cost_option_2 = cost_b + table_b_info["num_records"] * cost_inner_a

        if cost_option_1 <= cost_option_2:
            return table_a_info["name"], table_b_info["name"], cost_option_1
        else:
            return table_b_info["name"], table_a_info["name"], cost_option_2
