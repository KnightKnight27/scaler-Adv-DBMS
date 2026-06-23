"""constants.py — engine-wide tunables.

Keep these in one place so the storage layer, buffer pool, and B+ tree all agree
on sizes. Changing PAGE_SIZE here changes the unit of disk I/O, buffering, and
index nodes everywhere.
"""

from __future__ import annotations

# --- Storage geometry ---
PAGE_SIZE = 4096  # bytes per page (unit of disk I/O and buffering)

# Page ids are non-negative ints. INVALID_PAGE_ID marks "no page" (e.g. an empty
# next-pointer in a heap file or B+ tree sibling link).
INVALID_PAGE_ID = -1

# --- Buffer pool ---
DEFAULT_POOL_FRAMES = 64  # number of pages the buffer pool keeps resident

# --- Record / page layout ---
# Slotted-page slot directory entry = (offset:uint16, length:uint16) = 4 bytes.
SLOT_SIZE = 4
# A length/offset of this sentinel in a slot means "tombstone" (deleted record).
TOMBSTONE = 0xFFFF

# --- Encodings (struct format chars, all little-endian via "<") ---
INT_FMT = "<q"      # 8-byte signed integer
FLOAT_FMT = "<d"    # 8-byte IEEE double
BOOL_FMT = "<?"     # 1-byte boolean
LEN_FMT = "<I"      # 4-byte unsigned length prefix for variable-length fields
