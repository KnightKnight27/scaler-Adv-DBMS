#!/usr/bin/env python3
"""
Lab 4 - SQLite B-Tree Page Analyser
Reads campus.db and prints a human-readable breakdown of:
  - File header (page 1, bytes 0-99)
  - B-Tree page headers for every page
  - Cell pointer arrays
  - Decoded row data for the students table
"""

import struct
import sys
import os

DB_PATH = os.path.join(os.path.dirname(__file__), "campus.db")

# ─── Helpers ──────────────────────────────────────────────────────────────────

def read_u8(data, off):  return data[off]
def read_u16(data, off): return struct.unpack_from(">H", data, off)[0]
def read_u32(data, off): return struct.unpack_from(">I", data, off)[0]

def decode_varint(data, pos):
    """Decode a SQLite variable-length integer. Returns (value, bytes_consumed)."""
    result = 0
    for i in range(9):
        byte = data[pos + i]
        if i < 8:
            result = (result << 7) | (byte & 0x7F)
            if not (byte & 0x80):
                return result, i + 1
        else:
            result = (result << 8) | byte
            return result, 9
    return result, 9

# ─── File Header ──────────────────────────────────────────────────────────────

def parse_file_header(data):
    print("=" * 60)
    print("  SQLite File Header  (bytes 0-99)")
    print("=" * 60)
    magic = data[0:16]
    print(f"  Magic string       : {magic[:15].decode('ascii', errors='replace')!r}")
    page_size = read_u16(data, 16)
    if page_size == 1:
        page_size = 65536
    print(f"  Page size          : {page_size} bytes")
    print(f"  File format write  : {read_u8(data, 18)}")
    print(f"  File format read   : {read_u8(data, 19)}")
    print(f"  Reserved bytes/page: {read_u8(data, 20)}")
    print(f"  Max embedded frac  : {read_u8(data, 21)}")
    print(f"  Min embedded frac  : {read_u8(data, 22)}")
    print(f"  Leaf frac          : {read_u8(data, 23)}")
    print(f"  File change counter: {read_u32(data, 24)}")
    page_count = read_u32(data, 28)
    print(f"  Total page count   : {page_count}")
    print(f"  First freelist page: {read_u32(data, 32)}")
    print(f"  Freelist pages     : {read_u32(data, 36)}")
    print(f"  Schema cookie      : {read_u32(data, 40)}")
    print(f"  Schema format      : {read_u32(data, 44)}")
    print(f"  Default cache size : {read_u32(data, 48)}")
    print(f"  SQLite version     : {read_u32(data, 96)}")
    print()
    return page_size, page_count

# ─── B-Tree Page Header ───────────────────────────────────────────────────────

PAGE_TYPES = {
    0x02: "Interior Index B-Tree",
    0x05: "Interior Table B-Tree",
    0x0A: "Leaf Index B-Tree",
    0x0D: "Leaf Table B-Tree",
}

def parse_page_header(page_data, page_no, page_size, is_page1=False):
    off = 100 if is_page1 else 0
    page_type_byte = read_u8(page_data, off)
    page_type = PAGE_TYPES.get(page_type_byte, f"Unknown (0x{page_type_byte:02x})")
    first_freeblock = read_u16(page_data, off + 1)
    cell_count      = read_u16(page_data, off + 3)
    cell_content_off= read_u16(page_data, off + 5)
    if cell_content_off == 0:
        cell_content_off = 65536
    fragmented_free = read_u8(page_data,  off + 7)

    is_interior = page_type_byte in (0x02, 0x05)
    right_child = read_u32(page_data, off + 8) if is_interior else None

    print(f"  Page type          : {page_type}")
    print(f"  First freeblock    : {first_freeblock}")
    print(f"  Cell count         : {cell_count}")
    print(f"  Cell content offset: {cell_content_off}")
    print(f"  Fragmented bytes   : {fragmented_free}")
    if right_child:
        print(f"  Right-most child   : page {right_child}")

    header_size = 12 if is_interior else 8
    ptr_off = off + header_size
    cell_ptrs = [read_u16(page_data, ptr_off + i * 2) for i in range(cell_count)]
    print(f"  Cell pointers      : {cell_ptrs}")
    return cell_count, cell_ptrs, page_type_byte

# ─── Decode Leaf Table Cells ─────────────────────────────────────────────────

def decode_serial_type(stype):
    if stype == 0:   return "NULL",    0
    if stype == 1:   return "int8",    1
    if stype == 2:   return "int16",   2
    if stype == 3:   return "int24",   3
    if stype == 4:   return "int32",   4
    if stype == 5:   return "int48",   6
    if stype == 6:   return "int64",   8
    if stype == 7:   return "float64", 8
    if stype == 8:   return "0",       0
    if stype == 9:   return "1",       0
    if stype >= 12 and stype % 2 == 0:
        n = (stype - 12) // 2
        return f"blob({n})", n
    if stype >= 13 and stype % 2 == 1:
        n = (stype - 13) // 2
        return f"text({n})", n
    return f"?({stype})", 0

def read_int_be(data, pos, n):
    val = 0
    for i in range(n):
        val = (val << 8) | data[pos + i]
    # Sign-extend
    if n < 8 and val >= (1 << (n * 8 - 1)):
        val -= (1 << (n * 8))
    return val

def decode_leaf_cell(page_data, ptr):
    pos = ptr
    payload_len, nb = decode_varint(page_data, pos); pos += nb
    rowid,       nb = decode_varint(page_data, pos); pos += nb

    # Parse record header
    hdr_start = pos
    hdr_len, nb = decode_varint(page_data, pos); pos += nb
    serial_types = []
    while pos < hdr_start + hdr_len:
        st, nb = decode_varint(page_data, pos); pos += nb
        serial_types.append(st)

    # Parse record body
    values = []
    for st in serial_types:
        kind, size = decode_serial_type(st)
        if size == 0:
            val = kind  # NULL / 0 / 1 literal
        elif "text" in kind:
            val = page_data[pos:pos+size].decode("utf-8", errors="replace")
        elif "blob" in kind:
            val = page_data[pos:pos+size].hex()
        elif "float" in kind:
            val = struct.unpack_from(">d", page_data, pos)[0]
        else:
            val = read_int_be(page_data, pos, size)
        values.append(val)
        pos += size

    return rowid, values

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    with open(DB_PATH, "rb") as f:
        raw = f.read()

    page_size, page_count = parse_file_header(raw)

    for pg in range(1, page_count + 1):
        start = (pg - 1) * page_size
        page  = raw[start:start + page_size]
        is_p1 = (pg == 1)

        print("=" * 60)
        print(f"  Page {pg}  (offset 0x{start:06x})")
        print("=" * 60)

        cell_count, cell_ptrs, ptype = parse_page_header(page, pg, page_size, is_p1)

        # Decode rows for leaf table pages (skip sqlite_schema page 1)
        if ptype == 0x0D and cell_count > 0 and pg > 1:
            print()
            print("  Decoded Rows:")
            print(f"  {'id(rowid)':<10} {'name':<10} {'grade':<7} {'dept'}")
            print("  " + "-" * 40)
            for ptr in cell_ptrs:
                try:
                    rowid, vals = decode_leaf_cell(page, ptr)
                    # schema: id(PK/rowid) | name | grade | department
                    # SQLite stores NULL for id when it's an INTEGER PRIMARY KEY
                    # alias — the actual id value IS the rowid.
                    sid   = rowid   # id == rowid always for INTEGER PRIMARY KEY
                    name  = vals[1] if len(vals) > 1 else "?"
                    grade = vals[2] if len(vals) > 2 else "?"
                    dept  = vals[3] if len(vals) > 3 else "?"
                    print(f"  {sid:<10} {str(name):<10} {str(grade):<7} {dept}")
                except Exception as e:
                    print(f"  [decode error at ptr={ptr}: {e}]")
        print()

    print("=" * 60)
    print("  Analysis complete.")
    print("=" * 60)

if __name__ == "__main__":
    main()
