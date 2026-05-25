# SQLite Record Format Analysis

## Introduction

SQLite stores rows in a compact binary record format.

Each record contains:
1. Payload size
2. Row ID
3. Record header
4. Column values

SQLite uses variable-length encoding to reduce storage usage.

---

## General Record Structure

```text
+----------------------+
| Payload Size         |
+----------------------+
| Row ID               |
+----------------------+
| Record Header        |
+----------------------+
| Column Data          |
+----------------------+
```

---

## Example Record Bytes

Example bytes:

```text
15 01 04 00 17 01 61 6c 69 63 65
```

---

## Record Interpretation

| Bytes | Meaning |
|---|---|
| 15 | Payload size |
| 01 | Row ID |
| 04 | Header size |
| 00 | NULL serial type |
| 17 | TEXT serial type |
| 01 | INTEGER serial type |
| 61 6c 69 63 65 | "alice" |

---

## Serial Type Codes

SQLite uses serial type codes to identify column types.

Common serial types:

| Serial Type | Meaning |
|---|---|
| 0 | NULL |
| 1 | 1-byte integer |
| 2 | 2-byte integer |
| 3 | 3-byte integer |
| 4 | 4-byte integer |
| 5 | 6-byte integer |
| 6 | 8-byte integer |
| 13+ | TEXT |

---

## TEXT Storage

TEXT values are stored as UTF-8 byte sequences.

Example:

```text
61 6c 69 63 65
```

ASCII decoding:

```text
alice
```

---

## INTEGER Storage

INTEGER values use variable-length encoding.

Advantages:
- reduced storage usage
- compact records
- efficient serialization

Small integers consume fewer bytes.

---

## Payload Size

The payload size field specifies:
- total record payload length
- number of bytes occupied by record data

SQLite uses payload size during:
- page traversal
- record decoding
- overflow handling

---

## Variable-Length Integers

SQLite uses varints (variable-length integers).

Varints reduce storage overhead.

Advantages:
- smaller records
- compact pages
- reduced disk usage

---

## Summary

SQLite records are compact binary structures containing:
- metadata
- serial type information
- actual column values

This format enables:
- compact storage
- efficient traversal
- reduced page size usage
- optimized disk access