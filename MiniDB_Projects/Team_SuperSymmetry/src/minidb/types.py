"""
Type system, schemas, and binary tuple (record) serialization for MiniDB.

A Record is just a Python list of values. A Schema describes the columns.
Records are serialized to bytes with a leading NULL bitmap so that any
column may be NULL, followed by a type-specific encoding per column.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import Enum
from typing import Any, List, Optional, Tuple


class DataType(Enum):
    INT = "INT"
    FLOAT = "FLOAT"
    TEXT = "TEXT"
    BOOL = "BOOL"


@dataclass(frozen=True)
class Column:
    name: str
    type: DataType


class Schema:
    """An ordered collection of typed columns."""

    def __init__(self, columns: List[Column]):
        self.columns = columns
        self._index = {c.name: i for i, c in enumerate(columns)}

    def __len__(self) -> int:
        return len(self.columns)

    def index_of(self, name: str) -> int:
        if name not in self._index:
            raise KeyError(f"unknown column: {name}")
        return self._index[name]

    def names(self) -> List[str]:
        return [c.name for c in self.columns]

    def type_of(self, name: str) -> DataType:
        return self.columns[self.index_of(name)].type

    # ---- serialization ----------------------------------------------------
    def serialize(self, record: List[Any]) -> bytes:
        if len(record) != len(self.columns):
            raise ValueError(
                f"record arity {len(record)} != schema arity {len(self.columns)}"
            )
        n = len(self.columns)
        nbytes = (n + 7) // 8
        nullmap = bytearray(nbytes)
        body = bytearray()
        for i, (col, val) in enumerate(zip(self.columns, record)):
            if val is None:
                nullmap[i // 8] |= 1 << (i % 8)
                continue
            body += _encode_value(col.type, val)
        return bytes(nullmap) + bytes(body)

    def deserialize(self, data: bytes) -> List[Any]:
        n = len(self.columns)
        nbytes = (n + 7) // 8
        nullmap = data[:nbytes]
        offset = nbytes
        out: List[Any] = []
        for i, col in enumerate(self.columns):
            if nullmap[i // 8] & (1 << (i % 8)):
                out.append(None)
                continue
            val, offset = _decode_value(col.type, data, offset)
            out.append(val)
        return out


def _encode_value(t: DataType, val: Any) -> bytes:
    if t == DataType.INT:
        return struct.pack("<q", int(val))
    if t == DataType.FLOAT:
        return struct.pack("<d", float(val))
    if t == DataType.BOOL:
        return struct.pack("<B", 1 if val else 0)
    if t == DataType.TEXT:
        raw = str(val).encode("utf-8")
        return struct.pack("<H", len(raw)) + raw
    raise ValueError(f"unsupported type {t}")


def _decode_value(t: DataType, data: bytes, off: int) -> Tuple[Any, int]:
    if t == DataType.INT:
        return struct.unpack_from("<q", data, off)[0], off + 8
    if t == DataType.FLOAT:
        return struct.unpack_from("<d", data, off)[0], off + 8
    if t == DataType.BOOL:
        return bool(struct.unpack_from("<B", data, off)[0]), off + 1
    if t == DataType.TEXT:
        (ln,) = struct.unpack_from("<H", data, off)
        off += 2
        return data[off : off + ln].decode("utf-8"), off + ln
    raise ValueError(f"unsupported type {t}")


def coerce(t: DataType, raw: Any) -> Any:
    """Coerce a parsed literal into the proper python type for a column."""
    if raw is None:
        return None
    if t == DataType.INT:
        return int(raw)
    if t == DataType.FLOAT:
        return float(raw)
    if t == DataType.BOOL:
        if isinstance(raw, str):
            return raw.lower() in ("true", "1", "t")
        return bool(raw)
    if t == DataType.TEXT:
        return str(raw)
    return raw
