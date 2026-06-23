import re

class SQLParser:
    @staticmethod
    def parse(sql_str):
        # Clean whitespaces
        sql_str = sql_str.strip().replace('\n', ' ')
        sql_str = re.sub(r'\s+', ' ', sql_str)
        
        # Check CREATE TABLE
        create_match = re.match(r'^CREATE\s+TABLE\s+(\w+)\s*\((.*)\)$', sql_str, re.IGNORECASE)
        if create_match:
            table_name = create_match.group(1)
            columns_part = create_match.group(2)
            
            columns = []
            types = []
            pk = None
            
            # Simple column definition parser split by comma (need to ignore commas inside VARCHAR(N))
            # e.g. "id INT PRIMARY KEY, name VARCHAR(50)"
            col_defs = re.findall(r'(\w+)\s+(INT|VARCHAR\(\d+\))(?:\s+PRIMARY\s+KEY)?', columns_part, re.IGNORECASE)
            # Match columns with primary keys manually to get pk flag
            for col_def in columns_part.split(','):
                col_def = col_def.strip()
                match = re.match(r'(\w+)\s+(INT|VARCHAR\(\d+\))(.*)', col_def, re.IGNORECASE)
                if match:
                    col_name = match.group(1)
                    col_type = match.group(2).upper()
                    rest = match.group(3).upper()
                    
                    columns.append(col_name)
                    types.append(col_type)
                    if 'PRIMARY KEY' in rest:
                        pk = col_name
            return {
                "type": "CREATE",
                "table_name": table_name,
                "columns": columns,
                "types": types,
                "primary_key": pk
            }

        # Check INSERT INTO
        insert_match = re.match(r'^INSERT\s+INTO\s+(\w+)\s+VALUES\s*\((.*)\)$', sql_str, re.IGNORECASE)
        if insert_match:
            table_name = insert_match.group(1)
            values_str = insert_match.group(2)
            
            # Extract values, stripping quotes
            values = []
            for val in re.split(r',\s*', values_str):
                val = val.strip()
                if (val.startswith("'") and val.endswith("'")) or (val.startswith('"') and val.endswith('"')):
                    values.append(val[1:-1])
                elif val.isdigit():
                    values.append(int(val))
                elif val.replace('.', '', 1).isdigit():
                    values.append(float(val))
                else:
                    values.append(val)
            return {
                "type": "INSERT",
                "table_name": table_name,
                "values": values
            }

        # Check DELETE FROM
        delete_match = re.match(r'^DELETE\s+FROM\s+(\w+)(?:\s+WHERE\s+(.*))?$', sql_str, re.IGNORECASE)
        if delete_match:
            table_name = delete_match.group(1)
            where_clause = delete_match.group(2)
            
            where_cond = None
            if where_clause:
                where_cond = SQLParser._parse_where(where_clause)
                
            return {
                "type": "DELETE",
                "table_name": table_name,
                "where": where_cond
            }

        # Check SELECT
        # Pattern: SELECT <cols> FROM <t1> [JOIN <t2> ON <cond>] [WHERE <cond>]
        select_match = re.match(
            r'^SELECT\s+(.+?)\s+FROM\s+(\w+)(?:\s+JOIN\s+(\w+)\s+ON\s+(.+?))?(?:\s+WHERE\s+(.+))?$', 
            sql_str, 
            re.IGNORECASE
        )
        if select_match:
            cols_str = select_match.group(1)
            table_name = select_match.group(2)
            join_table = select_match.group(3)
            join_cond_str = select_match.group(4)
            where_clause = select_match.group(5)
            
            columns = [c.strip() for c in cols_str.split(',')]
            
            join_cond = None
            if join_table and join_cond_str:
                # e.g., t1.id = t2.id
                cond_match = re.match(r'([\w.]+)\s*=\s*([\w.]+)', join_cond_str)
                if cond_match:
                    join_cond = (cond_match.group(1), cond_match.group(2))
                    
            where_cond = None
            if where_clause:
                where_cond = SQLParser._parse_where(where_clause)
                
            return {
                "type": "SELECT",
                "columns": columns,
                "table_name": table_name,
                "join_table": join_table,
                "join_condition": join_cond,
                "where": where_cond
            }
            
        raise ValueError(f"Unsupported or malformed SQL statement: {sql_str}")

    @staticmethod
    def _parse_where(where_clause):
        # E.g. "col = val" or "col > val"
        match = re.match(r'([\w.]+)\s*([=<>]|>=|<=)\s*(.*)', where_clause)
        if match:
            col = match.group(1).strip()
            op = match.group(2).strip()
            val_str = match.group(3).strip()
            
            # Clean quotes
            if (val_str.startswith("'") and val_str.endswith("'")) or (val_str.startswith('"') and val_str.endswith('"')):
                val = val_str[1:-1]
            elif val_str.isdigit():
                val = int(val_str)
            else:
                val = val_str
            return (col, op, val)
        return None
