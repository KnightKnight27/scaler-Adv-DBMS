"""
Catalog: stores table schemas (column names, types, primary key).
Persisted as JSON. Loaded on startup.

Column types supported: INT, TEXT, FLOAT
"""
import json
import os


SUPPORTED_TYPES = {'INT', 'TEXT', 'FLOAT'}


class Column:
    def __init__(self, name: str, col_type: str, primary_key: bool = False):
        self.name = name
        self.col_type = col_type.upper()
        self.primary_key = primary_key

    def to_dict(self) -> dict:
        return {'name': self.name, 'type': self.col_type, 'primary_key': self.primary_key}

    @staticmethod
    def from_dict(d: dict) -> 'Column':
        return Column(d['name'], d['type'], d.get('primary_key', False))

    def cast(self, value):
        """Cast a string value to this column's Python type."""
        if value is None:
            return None
        if self.col_type == 'INT':
            return int(value)
        if self.col_type == 'FLOAT':
            return float(value)
        return str(value)


class TableSchema:
    def __init__(self, name: str, columns: list[Column]):
        self.name = name
        self.columns = columns
        self._col_map = {c.name: c for c in columns}

    def get_column(self, name: str) -> Column | None:
        return self._col_map.get(name)

    def primary_key_col(self) -> Column | None:
        for c in self.columns:
            if c.primary_key:
                return c
        return None

    def col_names(self) -> list[str]:
        return [c.name for c in self.columns]

    def to_dict(self) -> dict:
        return {'name': self.name, 'columns': [c.to_dict() for c in self.columns]}

    @staticmethod
    def from_dict(d: dict) -> 'TableSchema':
        cols = [Column.from_dict(c) for c in d['columns']]
        return TableSchema(d['name'], cols)

    def cast_row(self, row: dict) -> dict:
        """Cast all values in a row dict to correct Python types."""
        return {k: self._col_map[k].cast(v) if k in self._col_map else v
                for k, v in row.items()}


class Catalog:
    def __init__(self, path: str):
        self.path = path
        self._tables: dict[str, TableSchema] = {}
        self._load()

    def create_table(self, name: str, columns: list[Column]):
        if name in self._tables:
            raise ValueError(f"Table '{name}' already exists")
        schema = TableSchema(name, columns)
        self._tables[name] = schema
        self._save()
        return schema

    def get_table(self, name: str) -> TableSchema | None:
        return self._tables.get(name)

    def drop_table(self, name: str):
        if name not in self._tables:
            raise ValueError(f"Table '{name}' does not exist")
        del self._tables[name]
        self._save()

    def list_tables(self) -> list[str]:
        return list(self._tables.keys())

    def _save(self):
        data = {name: schema.to_dict() for name, schema in self._tables.items()}
        with open(self.path, 'w') as f:
            json.dump(data, f, indent=2)

    def _load(self):
        if not os.path.exists(self.path):
            return
        with open(self.path, 'r') as f:
            data = json.load(f)
        self._tables = {name: TableSchema.from_dict(d) for name, d in data.items()}
