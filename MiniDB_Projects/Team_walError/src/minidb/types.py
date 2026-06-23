"""types.py — column types, schemas, and the row <-> bytes codec.

The record format is self-describing *given the schema*: to decode bytes you must
know the schema (column order + types). Layout of one encoded row:

    [ null bitmap : ceil(ncols/8) bytes ]
    [ for each NON-NULL column, in schema order: encoded value ]

Per-type value encoding (little-endian):
    INT   -> 8-byte signed   (struct "<q")
    FLOAT -> 8-byte double    (struct "<d")
    BOOL  -> 1-byte           (struct "<?")
    TEXT  -> 4-byte length prefix + UTF-8 bytes

A null bitmap (1 bit per column, bit set = NULL) keeps NULLs cheap and lets a
NULL column occupy zero value bytes. This mirrors how PostgreSQL stores its
per-tuple null bitmap in the heap tuple header.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import Enum
from typing import Any

from .constants import BOOL_FMT, FLOAT_FMT, INT_FMT, LEN_FMT


class ColumnType(Enum):
    """Supported column types."""

    INT = "INT"
    FLOAT = "FLOAT"
    TEXT = "TEXT"
    BOOL = "BOOL"


@dataclass(frozen=True)
class Column:
    """One column definition: name, type, nullability, primary-key flag."""

    name: str
    type: ColumnType
    nullable: bool = True
    primary_key: bool = False


class Schema:
    """An ordered list of columns describing a table/tuple shape."""

    def __init__(self, columns: list[Column]) -> None:
        if not columns:
            raise ValueError("schema must have at least one column")
        names = [c.name for c in columns]
        if len(names) != len(set(names)):
            raise ValueError(f"duplicate column names in schema: {names}")
        self.columns = list(columns)
        self._index = {c.name: i for i, c in enumerate(columns)}

    def __len__(self) -> int:
        return len(self.columns)

    @property
    def names(self) -> list[str]:
        return [c.name for c in self.columns]

    def index_of(self, name: str) -> int:
        if name not in self._index:
            raise KeyError(f"no such column: {name!r} (have {self.names})")
        return self._index[name]

    def column(self, name: str) -> Column:
        return self.columns[self.index_of(name)]

    def primary_key_index(self) -> int | None:
        for i, c in enumerate(self.columns):
            if c.primary_key:
                return i
        return None

    # --- codec ---------------------------------------------------------------

    def encode(self, row: tuple[Any, ...]) -> bytes:
        """Serialize a row (tuple of Python values) to bytes per this schema."""
        if len(row) != len(self.columns):
            raise ValueError(
                f"row has {len(row)} values, schema has {len(self.columns)} columns"
            )
        nbitmap = (len(self.columns) + 7) // 8
        bitmap = bytearray(nbitmap)
        body = bytearray()
        for i, (col, val) in enumerate(zip(self.columns, row)):
            if val is None:
                if not col.nullable:
                    raise ValueError(f"column {col.name!r} is NOT NULL but got None")
                bitmap[i // 8] |= 1 << (i % 8)
                continue
            body += _encode_value(col.type, val)
        return bytes(bitmap) + bytes(body)

    def decode(self, data: bytes) -> tuple[Any, ...]:
        """Deserialize bytes back into a row tuple per this schema."""
        nbitmap = (len(self.columns) + 7) // 8
        bitmap = data[:nbitmap]
        offset = nbitmap
        values: list[Any] = []
        for i, col in enumerate(self.columns):
            is_null = (bitmap[i // 8] >> (i % 8)) & 1
            if is_null:
                values.append(None)
                continue
            val, offset = _decode_value(col.type, data, offset)
            values.append(val)
        return tuple(values)


# --- value-level codec (module-private) --------------------------------------


def _encode_value(ctype: ColumnType, val: Any) -> bytes:
    if ctype is ColumnType.INT:
        if isinstance(val, bool) or not isinstance(val, int):
            raise TypeError(f"INT column requires int, got {type(val).__name__}")
        return struct.pack(INT_FMT, val)
    if ctype is ColumnType.FLOAT:
        return struct.pack(FLOAT_FMT, float(val))
    if ctype is ColumnType.BOOL:
        return struct.pack(BOOL_FMT, bool(val))
    if ctype is ColumnType.TEXT:
        raw = str(val).encode("utf-8")
        return struct.pack(LEN_FMT, len(raw)) + raw
    raise ValueError(f"unknown column type: {ctype}")


def _decode_value(ctype: ColumnType, data: bytes, offset: int) -> tuple[Any, int]:
    if ctype is ColumnType.INT:
        (v,) = struct.unpack_from(INT_FMT, data, offset)
        return v, offset + 8
    if ctype is ColumnType.FLOAT:
        (v,) = struct.unpack_from(FLOAT_FMT, data, offset)
        return v, offset + 8
    if ctype is ColumnType.BOOL:
        (v,) = struct.unpack_from(BOOL_FMT, data, offset)
        return v, offset + 1
    if ctype is ColumnType.TEXT:
        (n,) = struct.unpack_from(LEN_FMT, data, offset)
        start = offset + 4
        raw = data[start : start + n]
        return raw.decode("utf-8"), start + n
    raise ValueError(f"unknown column type: {ctype}")
