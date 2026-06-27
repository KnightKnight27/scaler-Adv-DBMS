"""Type system, table schema, and tuple (de)serialisation.

MiniDB supports four column types — ``INT`` (64-bit signed), ``FLOAT``
(64-bit IEEE double), ``TEXT`` (variable-length UTF-8, up to 64 KiB), and
``BOOL`` — any of which may be nullable.

A tuple is stored as:

    [ null bitmap | value | value | ... ]

The null bitmap (one bit per column, ceil(n/8) bytes) records which columns
are NULL; only non-null columns contribute bytes to the value area. Fixed
types use a fixed width; TEXT is length-prefixed. This compact layout is what
the heap file stores as an opaque record.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import Enum
from typing import Any, Dict, List, Optional


class DataType(str, Enum):
    INT = "INT"
    FLOAT = "FLOAT"
    TEXT = "TEXT"
    BOOL = "BOOL"


_INT = struct.Struct("<q")
_FLOAT = struct.Struct("<d")
_LEN = struct.Struct("<H")


@dataclass(frozen=True)
class Column:
    name: str
    type: DataType
    nullable: bool = True

    def to_dict(self) -> dict:
        return {"name": self.name, "type": self.type.value, "nullable": self.nullable}

    @classmethod
    def from_dict(cls, d: dict) -> "Column":
        return cls(d["name"], DataType(d["type"]), d.get("nullable", True))


class Schema:
    """An ordered list of columns with name<->index lookup."""

    def __init__(self, columns: List[Column]):
        if not columns:
            raise ValueError("a schema needs at least one column")
        names = [c.name for c in columns]
        if len(names) != len(set(names)):
            raise ValueError("duplicate column names in schema")
        self.columns = columns
        self._index = {c.name: i for i, c in enumerate(columns)}

    def __len__(self) -> int:
        return len(self.columns)

    def index_of(self, name: str) -> int:
        if name not in self._index:
            raise KeyError(f"no such column: {name}")
        return self._index[name]

    def has_column(self, name: str) -> bool:
        return name in self._index

    def column(self, name: str) -> Column:
        return self.columns[self.index_of(name)]

    @property
    def names(self) -> List[str]:
        return [c.name for c in self.columns]

    def to_dict(self) -> dict:
        return {"columns": [c.to_dict() for c in self.columns]}

    @classmethod
    def from_dict(cls, d: dict) -> "Schema":
        return cls([Column.from_dict(c) for c in d["columns"]])

    # -- value coercion -----------------------------------------------------
    def coerce(self, name: str, value: Any) -> Any:
        """Validate/normalise *value* for column *name* (raises on mismatch)."""
        col = self.column(name)
        if value is None:
            if not col.nullable:
                raise ValueError(f"column {name!r} is NOT NULL")
            return None
        try:
            if col.type is DataType.INT:
                return int(value)
            if col.type is DataType.FLOAT:
                return float(value)
            if col.type is DataType.BOOL:
                if isinstance(value, str):
                    return value.lower() in ("true", "t", "1")
                return bool(value)
            return str(value)  # TEXT
        except (TypeError, ValueError):
            raise ValueError(f"value {value!r} is not valid for {col.type.value} column {name!r}")

    # -- tuple codec --------------------------------------------------------
    def serialize(self, row: Dict[str, Any]) -> bytes:
        n = len(self.columns)
        bitmap = bytearray((n + 7) // 8)
        body = bytearray()
        for i, col in enumerate(self.columns):
            value = row.get(col.name)
            if value is None:
                if not col.nullable:
                    raise ValueError(f"column {col.name!r} is NOT NULL")
                bitmap[i // 8] |= (1 << (i % 8))
                continue
            if col.type is DataType.INT:
                body += _INT.pack(int(value))
            elif col.type is DataType.FLOAT:
                body += _FLOAT.pack(float(value))
            elif col.type is DataType.BOOL:
                body += b"\x01" if value else b"\x00"
            else:  # TEXT
                raw = str(value).encode("utf-8")
                if len(raw) > 0xFFFF:
                    raise ValueError("TEXT value exceeds 64 KiB")
                body += _LEN.pack(len(raw)) + raw
        return bytes(bitmap) + bytes(body)

    def deserialize(self, data: bytes) -> Dict[str, Any]:
        n = len(self.columns)
        nbytes = (n + 7) // 8
        bitmap = data[:nbytes]
        pos = nbytes
        row: Dict[str, Any] = {}
        for i, col in enumerate(self.columns):
            if bitmap[i // 8] & (1 << (i % 8)):
                row[col.name] = None
                continue
            if col.type is DataType.INT:
                (row[col.name],) = _INT.unpack_from(data, pos)
                pos += _INT.size
            elif col.type is DataType.FLOAT:
                (row[col.name],) = _FLOAT.unpack_from(data, pos)
                pos += _FLOAT.size
            elif col.type is DataType.BOOL:
                row[col.name] = data[pos] != 0
                pos += 1
            else:  # TEXT
                (length,) = _LEN.unpack_from(data, pos)
                pos += _LEN.size
                row[col.name] = data[pos:pos + length].decode("utf-8")
                pos += length
        return row
