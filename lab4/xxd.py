#!/usr/bin/env python3
"""
xxd.py — Custom hexadecimal file inspector
Lab 4: SQLite3 Internal Structure Analysis

Usage:
    python3 xxd.py students.db                        # full dump
    python3 xxd.py students.db --start 0 --length 100 # bytes 0–99
    python3 xxd.py students.db --start 4096 --length 32 # Page 2 header
    python3 xxd.py students.db --page 1               # entire Page 1
    python3 xxd.py students.db --page 2               # entire Page 2

Output format mirrors the standard xxd tool:
    00000000: 53 51 4c 69 74 65 20 66  6f 72 6d 61 74 20 33 00  SQLite format 3.
"""

import argparse
import sys

PAGE_SIZE = 4096  # default SQLite page size; override with --page-size


def xxd_dump(data: bytes, start_offset: int = 0, cols: int = 16) -> None:
    """Print a hex dump of data starting at the given file offset."""
    for i in range(0, len(data), cols):
        chunk = data[i:i + cols]
        file_offset = start_offset + i

        # Hex pairs, split into two groups of 8 for readability
        hex_left  = " ".join(f"{b:02x}" for b in chunk[:8])
        hex_right = " ".join(f"{b:02x}" for b in chunk[8:])
        hex_part  = f"{hex_left:<23}  {hex_right:<23}"

        # ASCII representation (printable chars only)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)

        print(f"{file_offset:08x}: {hex_part}  {ascii_part}")


def main():
    parser = argparse.ArgumentParser(
        description="xxd.py — hex inspector for SQLite (and any binary) files"
    )
    parser.add_argument("file", help="Path to the binary file to inspect")
    parser.add_argument("--start",     type=lambda x: int(x, 0), default=None,
                        help="Start byte offset (decimal or 0x hex). Default: 0")
    parser.add_argument("--length",    type=lambda x: int(x, 0), default=None,
                        help="Number of bytes to display. Default: all")
    parser.add_argument("--page",      type=int, default=None,
                        help="Display a full SQLite page by 1-based page number")
    parser.add_argument("--page-size", type=int, default=PAGE_SIZE,
                        help=f"SQLite page size in bytes (default: {PAGE_SIZE})")
    args = parser.parse_args()

    try:
        with open(args.file, "rb") as f:
            raw = f.read()
    except FileNotFoundError:
        print(f"Error: file not found: {args.file}", file=sys.stderr)
        sys.exit(1)

    file_size = len(raw)
    page_size = args.page_size

    # Resolve --page into --start / --length
    if args.page is not None:
        if args.page < 1:
            print("Error: --page is 1-based; minimum is 1", file=sys.stderr)
            sys.exit(1)
        start  = (args.page - 1) * page_size
        length = page_size
    else:
        start  = args.start  if args.start  is not None else 0
        length = args.length if args.length is not None else file_size - start

    end = min(start + length, file_size)

    if start >= file_size:
        print(f"Error: start offset {start} is beyond file size {file_size}",
              file=sys.stderr)
        sys.exit(1)

    # Print metadata header
    total_pages = (file_size + page_size - 1) // page_size
    print(f"# File : {args.file}  ({file_size} bytes, {total_pages} pages × {page_size} B)")
    print(f"# Range: offset {start:#010x} ({start}) → {end:#010x} ({end})  "
          f"[{end - start} bytes]\n")

    xxd_dump(raw[start:end], start_offset=start)


if __name__ == "__main__":
    main()
