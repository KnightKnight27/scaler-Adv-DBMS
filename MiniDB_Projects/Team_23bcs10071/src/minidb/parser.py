import re

class SQLParser:
    @staticmethod
    def parse(sql):
        sql = sql.strip().rstrip(';').strip()
        
        # 1. INSERT INTO table VALUES (val1, val2, ...)
        insert_match = re.match(r"(?i)^INSERT\s+INTO\s+(\w+)\s+VALUES\s*\((.+)\)$", sql)
        if insert_match:
            table = insert_match.group(1)
            raw_vals = insert_match.group(2)
            vals = []
            for v in re.findall(r"'[^']*'|\"[^\"]*\"|[^,\s]+", raw_vals):
                v = v.strip()
                if (v.startswith("'") and v.endswith("'")) or (v.startswith('"') and v.endswith('"')):
                    vals.append(v[1:-1])
                else:
                    try:
                        vals.append(int(v))
                    except ValueError:
                        vals.append(v)
            return {
                "type": "INSERT",
                "table": table,
                "values": vals
            }

        # 2. DELETE FROM table WHERE col = val
        delete_match = re.match(r"(?i)^DELETE\s+FROM\s+(\w+)(?:\s+WHERE\s+([\w\.]+)\s*=\s*(.+))?$", sql)
        if delete_match:
            table = delete_match.group(1)
            where_col = delete_match.group(2)
            where_val = delete_match.group(3)
            if where_val:
                where_val = where_val.strip()
                if (where_val.startswith("'") and where_val.endswith("'")) or (where_val.startswith('"') and where_val.endswith('"')):
                    where_val = where_val[1:-1]
                else:
                    try:
                        where_val = int(where_val)
                    except ValueError:
                        pass
            return {
                "type": "DELETE",
                "table": table,
                "where_col": where_col,
                "where_val": where_val
            }

        # 3. SELECT cols FROM table [JOIN table2 ON col1 = col2] [WHERE col3 = val3]
        select_match = re.match(r"(?i)^SELECT\s+(.+?)\s+FROM\s+(.+)$", sql)
        if select_match:
            cols_str = select_match.group(1).strip()
            rest = select_match.group(2).strip()
            cols = [c.strip() for c in cols_str.split(",")]
            
            # Extract WHERE if present
            where_col = None
            where_op = None
            where_val = None
            where_match = re.search(r"(?i)\s+WHERE\s+(.+)$", rest)
            if where_match:
                where_clause = where_match.group(1).strip()
                rest = rest[:where_match.start()].strip()
                cond_match = re.match(r"([\w\.]+)\s*(>=|<=|=|>|<)\s*(.+)$", where_clause)
                if cond_match:
                    where_col = cond_match.group(1).strip()
                    where_op = cond_match.group(2).strip()
                    where_val = cond_match.group(3).strip()
                    if (where_val.startswith("'") and where_val.endswith("'")) or (where_val.startswith('"') and where_val.endswith('"')):
                        where_val = where_val[1:-1]
                    else:
                        try:
                            where_val = int(where_val)
                        except ValueError:
                            pass
                else:
                    parts = where_clause.split("=")
                    where_col = parts[0].strip()
                    where_op = "="
                    where_val = parts[1].strip()
                    if (where_val.startswith("'") and where_val.endswith("'")) or (where_val.startswith('"') and where_val.endswith('"')):
                        where_val = where_val[1:-1]
                    else:
                        try:
                            where_val = int(where_val)
                        except ValueError:
                            pass

            # Extract JOIN if present
            join_table = None
            join_col1 = None
            join_col2 = None
            join_match = re.search(r"(?i)\s+JOIN\s+(\w+)\s+ON\s+(\S+)\s*=\s*(\S+)$", rest)
            if join_match:
                join_table = join_match.group(1).strip()
                join_col1 = join_match.group(2).strip()
                join_col2 = join_match.group(3).strip()
                table = rest[:join_match.start()].strip()
            else:
                table = rest
                
            return {
                "type": "SELECT",
                "columns": cols,
                "table": table,
                "join_table": join_table,
                "join_col1": join_col1,
                "join_col2": join_col2,
                "where_col": where_col,
                "where_op": where_op,
                "where_val": where_val
            }
            
        raise ValueError(f"Unsupported or malformed SQL statement: {sql}")
