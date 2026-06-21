import re
from typing import List, Dict, Any, Union

def tokenize(sql: str) -> List[str]:
    # Match strings (with optional escaped quotes), numbers, identifiers/dotted paths, comparison operators, and punctuation.
    pattern = re.compile(
        r"'(?:''|[^'])*'|\d+|!=|>=|<=|>|<|=|[a-zA-Z_][a-zA-Z0-9_\.]*|[,();\*]|\S"
    )
    tokens = []
    for match in pattern.finditer(sql):
        token = match.group(0)
        # Skip purely whitespace tokens unless they are within string literals (which don't get matched by \S but by the string regex)
        if token.strip() or token.startswith("'"):
            tokens.append(token)
    return tokens

class SQLParser:
    def __init__(self, sql: str = ""):
        self.tokens = tokenize(sql)
        self.idx = 0

    def current_token(self) -> Union[str, None]:
        if self.idx < len(self.tokens):
            return self.tokens[self.idx]
        return None

    def peek_token(self, offset: int = 1) -> Union[str, None]:
        if self.idx + offset < len(self.tokens):
            return self.tokens[self.idx + offset]
        return None

    def consume_token(self, expected: str = None) -> str:
        tok = self.current_token()
        if tok is None:
            raise ValueError("Unexpected End of SQL Input")
        if expected is not None:
            if tok.upper() != expected.upper():
                raise ValueError(f"Expected token '{expected}', got '{tok}'")
        self.idx += 1
        return tok

    def match_keyword(self, kw: str) -> bool:
        tok = self.current_token()
        if tok and tok.upper() == kw.upper():
            self.consume_token()
            return True
        return False

    def parse(self, sql: str = None) -> Dict[str, Any]:
        if sql is not None:
            self.tokens = tokenize(sql)
            self.idx = 0
            
        tok = self.current_token()
        if tok is None:
            raise ValueError("Empty SQL query")

        tok_upper = tok.upper()
        if tok_upper == "CREATE":
            return self.parse_create_table()
        elif tok_upper == "INSERT":
            return self.parse_insert()
        elif tok_upper == "SELECT":
            return self.parse_select()
        elif tok_upper == "DELETE":
            return self.parse_delete()
        elif tok_upper == "BEGIN":
            self.consume_token("BEGIN")
            self.match_keyword(";")
            return {"type": "BeginTxn"}
        elif tok_upper == "COMMIT":
            self.consume_token("COMMIT")
            self.match_keyword(";")
            return {"type": "CommitTxn"}
        elif tok_upper == "ROLLBACK":
            self.consume_token("ROLLBACK")
            self.match_keyword(";")
            return {"type": "RollbackTxn"}
        else:
            raise ValueError(f"Unknown SQL command starting with '{tok}'")

    def parse_create_table(self) -> Dict[str, Any]:
        self.consume_token("CREATE")
        self.consume_token("TABLE")
        table_name = self.consume_token()
        self.consume_token("(")
        
        columns = []
        while True:
            col_name = self.consume_token()
            col_type = self.consume_token()
            
            primary_key = False
            # Check for primary key constraint
            if self.current_token() and self.current_token().upper() == "PRIMARY":
                self.consume_token("PRIMARY")
                self.consume_token("KEY")
                primary_key = True

            columns.append({
                "name": col_name,
                "type": col_type.upper(),
                "primary_key": primary_key
            })

            if self.current_token() == ",":
                self.consume_token(",")
            elif self.current_token() == ")":
                self.consume_token(")")
                break
            else:
                raise ValueError(f"Expected ',' or ')' in CREATE TABLE, got '{self.current_token()}'")

        self.match_keyword(";")
        return {
            "type": "CreateTable",
            "table": table_name,
            "table_name": table_name,
            "columns": columns
        }

    def parse_insert(self) -> Dict[str, Any]:
        self.consume_token("INSERT")
        self.consume_token("INTO")
        table_name = self.consume_token()
        self.consume_token("VALUES")
        self.consume_token("(")
        
        values = []
        while True:
            tok = self.consume_token()
            if tok.startswith("'") and tok.endswith("'"):
                # String literal: unescape double single-quotes
                val = tok[1:-1].replace("''", "'")
            elif tok.isdigit():
                val = int(tok)
            elif (tok.startswith("-") or tok.startswith("+")) and tok[1:].isdigit():
                val = int(tok)
            else:
                # Treat as identifier or text block
                val = tok
            
            values.append(val)

            if self.current_token() == ",":
                self.consume_token(",")
            elif self.current_token() == ")":
                self.consume_token(")")
                break
            else:
                raise ValueError(f"Expected ',' or ')' in INSERT values, got '{self.current_token()}'")

        self.match_keyword(";")
        return {
            "type": "Insert",
            "table": table_name,
            "table_name": table_name,
            "values": values
        }

    def parse_select(self) -> Dict[str, Any]:
        self.consume_token("SELECT")
        
        columns = []
        if self.current_token() == "*":
            columns.append(self.consume_token("*"))
        else:
            while True:
                columns.append(self.consume_token())
                if self.current_token() == ",":
                    self.consume_token(",")
                else:
                    break

        self.consume_token("FROM")
        table_name = self.consume_token()
        
        # Optional table alias
        alias = None
        next_tok = self.current_token()
        if next_tok and next_tok.upper() not in ("JOIN", "WHERE", ";"):
            alias = self.consume_token()

        join_info = None
        if self.match_keyword("JOIN"):
            join_table = self.consume_token()
            
            join_alias = None
            if self.current_token() and self.current_token().upper() != "ON":
                join_alias = self.consume_token()
                
            self.consume_token("ON")
            left_col = self.consume_token()
            op = self.consume_token()  # usually '='
            right_col = self.consume_token()
            
            join_info = {
                "table": join_table,
                "alias": join_alias,
                "on_left": left_col,
                "op": op,
                "on_right": right_col
            }

        where_info = None
        if self.match_keyword("WHERE"):
            left = self.consume_token()
            op = self.consume_token()
            
            val_tok = self.consume_token()
            if val_tok.startswith("'") and val_tok.endswith("'"):
                val = val_tok[1:-1].replace("''", "'")
            elif val_tok.isdigit():
                val = int(val_tok)
            elif (val_tok.startswith("-") or val_tok.startswith("+")) and val_tok[1:].isdigit():
                val = int(val_tok)
            else:
                val = val_tok
                
            where_info = {
                "left": left,
                "op": op,
                "right": val
            }

        self.match_keyword(";")
        return {
            "type": "Select",
            "columns": columns,
            "from_table": table_name,
            "from_alias": alias,
            "join": join_info,
            "where": where_info
        }

    def parse_delete(self) -> Dict[str, Any]:
        self.consume_token("DELETE")
        self.consume_token("FROM")
        table_name = self.consume_token()
        
        where_info = None
        if self.match_keyword("WHERE"):
            left = self.consume_token()
            op = self.consume_token()
            
            val_tok = self.consume_token()
            if val_tok.startswith("'") and val_tok.endswith("'"):
                val = val_tok[1:-1].replace("''", "'")
            elif val_tok.isdigit():
                val = int(val_tok)
            elif (val_tok.startswith("-") or val_tok.startswith("+")) and val_tok[1:].isdigit():
                val = int(val_tok)
            else:
                val = val_tok
                
            where_info = {
                "left": left,
                "op": op,
                "right": val
            }

        self.match_keyword(";")
        return {
            "type": "Delete",
            "table": table_name,
            "table_name": table_name,
            "where": where_info
        }
