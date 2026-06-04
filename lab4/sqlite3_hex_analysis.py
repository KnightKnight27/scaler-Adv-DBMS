"""
Lab 4: SQLite3 Database Internal Structure Analysis Using XXD
Student: Talin Daga (24bcs10321)

Requires: Python 3, xxd (macOS built-in; Linux: sudo apt install xxd)
Run:      python3 sqlite3_hex_analysis.py
"""

import os
import sqlite3
import struct
import subprocess
import sys

DB_FILE = "lab4_analysis.db"

# ── SQLite3 file-format reference constants ───────────────────────
PAGE_TYPES = {
    0x02: "Interior Index B-tree",
    0x05: "Interior Table B-tree",
    0x0A: "Leaf Index B-tree",
    0x0D: "Leaf Table B-tree",
}
TEXT_ENC = {1: "UTF-8", 2: "UTF-16le", 3: "UTF-16be"}


# ── Helpers ───────────────────────────────────────────────────────

def sep(title):
    print(f"\n{'='*70}")
    print(f"  {title}")
    print(f"{'='*70}")


def xxd(start=0, length=None, cols=16):
    """Return xxd hex dump of DB_FILE as an indented string."""
    cmd = ["xxd", "-c", str(cols)]
    if start:
        cmd += ["-s", str(start)]
    if length is not None:
        cmd += ["-l", str(length)]
    cmd.append(DB_FILE)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return f"  (xxd error: {r.stderr.strip()})"
    return "\n".join("    " + ln for ln in r.stdout.splitlines())


def read_bytes(offset, n):
    with open(DB_FILE, "rb") as f:
        f.seek(offset)
        return f.read(n)


def u8(data, off):  return data[off]
def u16(data, off): return struct.unpack_from(">H", data, off)[0]
def u32(data, off): return struct.unpack_from(">I", data, off)[0]


def page_size_from_header(hdr):
    raw = u16(hdr, 16)
    return 65536 if raw == 1 else raw


# ── Task 1: Database Creation ─────────────────────────────────────

def task1_create_database():
    sep("TASK 1: Database Creation")

    if os.path.exists(DB_FILE):
        os.remove(DB_FILE)

    conn = sqlite3.connect(DB_FILE)
    cur  = conn.cursor()

    cur.execute("""
        CREATE TABLE students (
            id      INTEGER PRIMARY KEY,
            name    TEXT    NOT NULL,
            roll_no TEXT    UNIQUE,
            branch  TEXT,
            cgpa    REAL
        )
    """)

    records = [
        (1,  "Alice Sharma",  "24BCS10001", "CSE", 9.1),
        (2,  "Bob Mehta",     "24BCS10002", "CSE", 8.7),
        (3,  "Clara Singh",   "24BCS10003", "ECE", 8.3),
        (4,  "David Kumar",   "24BCS10004", "CSE", 9.5),
        (5,  "Eva Patel",     "24BCS10005", "IT",  7.8),
        (6,  "Frank Joshi",   "24BCS10006", "ECE", 8.9),
        (7,  "Gita Rao",      "24BCS10007", "CSE", 9.2),
        (8,  "Hina Khan",     "24BCS10008", "IT",  8.1),
        (9,  "Ivan Verma",    "24BCS10009", "CSE", 7.6),
        (10, "Jasmine Nair",  "24BCS10010", "ECE", 9.0),
    ]
    cur.executemany("INSERT INTO students VALUES (?,?,?,?,?)", records)
    conn.commit()

    print(f"\n  Database file : {DB_FILE}")
    print(f"  File size     : {os.path.getsize(DB_FILE):,} bytes\n")
    print("  Schema:")
    print("    CREATE TABLE students (")
    print("        id      INTEGER PRIMARY KEY,")
    print("        name    TEXT    NOT NULL,")
    print("        roll_no TEXT    UNIQUE,")
    print("        branch  TEXT,")
    print("        cgpa    REAL")
    print("    )")
    print(f"\n  Records inserted: {len(records)}")
    print(f"\n  {'ID':<5} {'Name':<16} {'Roll No':<14} {'Branch':<8} CGPA")
    print(f"  {'-'*52}")
    for r in records:
        print(f"  {r[0]:<5} {r[1]:<16} {r[2]:<14} {r[3]:<8} {r[4]}")

    conn.close()


# ── Task 2: Database Metadata Analysis ───────────────────────────

def task2_metadata():
    sep("TASK 2: Database Metadata Analysis")
    conn = sqlite3.connect(DB_FILE)
    cur  = conn.cursor()

    print("\n  PRAGMA values:")
    print(f"  {'Pragma':<22} {'Value':<16} Description")
    print(f"  {'-'*65}")
    pragmas = [
        ("page_size",      "Bytes per page"),
        ("page_count",     "Total pages in the file"),
        ("freelist_count", "Unused (free) pages"),
        ("encoding",       "Text encoding"),
        ("journal_mode",   "Journal / WAL mode"),
        ("schema_version", "Schema cookie — increments on every DDL"),
        ("application_id", "Application-defined identifier"),
    ]
    for p, desc in pragmas:
        cur.execute(f"PRAGMA {p}")
        val = cur.fetchone()[0]
        print(f"  {p:<22} {str(val):<16} {desc}")

    print("\n  sqlite_master (internal schema catalog):")
    cur.execute(
        "SELECT type, name, tbl_name, rootpage FROM sqlite_master ORDER BY rootpage"
    )
    rows = cur.fetchall()
    print(f"  {'Type':<10} {'Name':<34} {'Table':<18} Root page")
    print(f"  {'-'*68}")
    for r in rows:
        print(f"  {r[0]:<10} {r[1]:<34} {r[2]:<18} {r[3]}")

    print("\n  Observation: page 1 is always the root of sqlite_master.")
    print("  User tables and indexes live on the pages listed above.")
    conn.close()


# ── Task 3: SQLite File Header Inspection ────────────────────────

def task3_file_header():
    sep("TASK 3: SQLite File Header Inspection (bytes 0 – 99)")

    print("\n  Raw xxd dump of the first 100 bytes:\n")
    print(xxd(start=0, length=100))

    hdr = read_bytes(0, 100)
    ps  = page_size_from_header(hdr)

    print("\n  Annotated header fields:")
    print(f"  {'Offset':<10} {'Hex':<26} {'Field':<32} Decoded value")
    print(f"  {'-'*80}")

    magic = hdr[0:16]
    print(f"  {'0–15':<10} {magic.hex()[:24]+'...':<26} {'Magic string':<32} {repr(magic.decode('latin-1'))}")

    ps_raw = u16(hdr, 16)
    print(f"  {'16–17':<10} {hdr[16:18].hex():<26} {'Page size':<32} {ps} bytes  (raw={ps_raw}, 1 means 65536)")
    print(f"  {'18':<10} {hdr[18:19].hex():<26} {'Write format version':<32} {u8(hdr,18)}  (1=legacy, 2=WAL)")
    print(f"  {'19':<10} {hdr[19:20].hex():<26} {'Read format version':<32} {u8(hdr,19)}")
    print(f"  {'20':<10} {hdr[20:21].hex():<26} {'Reserved bytes per page':<32} {u8(hdr,20)}")
    print(f"  {'21':<10} {hdr[21:22].hex():<26} {'Max embedded payload frac':<32} {u8(hdr,21)}  (must be 64)")
    print(f"  {'22':<10} {hdr[22:23].hex():<26} {'Min embedded payload frac':<32} {u8(hdr,22)}  (must be 32)")
    print(f"  {'23':<10} {hdr[23:24].hex():<26} {'Leaf payload fraction':<32} {u8(hdr,23)}  (must be 32)")
    print(f"  {'24–27':<10} {hdr[24:28].hex():<26} {'File change counter':<32} {u32(hdr,24)}")
    print(f"  {'28–31':<10} {hdr[28:32].hex():<26} {'Database size (pages)':<32} {u32(hdr,28)}")
    print(f"  {'32–35':<10} {hdr[32:36].hex():<26} {'First freelist trunk page':<32} {u32(hdr,32)}")
    print(f"  {'36–39':<10} {hdr[36:40].hex():<26} {'Total freelist pages':<32} {u32(hdr,36)}")
    print(f"  {'40–43':<10} {hdr[40:44].hex():<26} {'Schema cookie':<32} {u32(hdr,40)}")
    print(f"  {'44–47':<10} {hdr[44:48].hex():<26} {'Schema format number':<32} {u32(hdr,44)}  (4=current)")
    print(f"  {'48–51':<10} {hdr[48:52].hex():<26} {'Default page cache size':<32} {u32(hdr,48)}")
    print(f"  {'52–55':<10} {hdr[52:56].hex():<26} {'Largest root B-tree page':<32} {u32(hdr,52)}")
    enc_val = u32(hdr, 56)
    print(f"  {'56–59':<10} {hdr[56:60].hex():<26} {'Text encoding':<32} {enc_val}  ({TEXT_ENC.get(enc_val,'?')})")
    print(f"  {'60–63':<10} {hdr[60:64].hex():<26} {'User version':<32} {u32(hdr,60)}")
    print(f"  {'64–67':<10} {hdr[64:68].hex():<26} {'Incremental vacuum mode':<32} {u32(hdr,64)}")
    print(f"  {'68–71':<10} {hdr[68:72].hex():<26} {'Application ID':<32} {u32(hdr,68)}")
    ver = u32(hdr, 96)
    print(f"  {'92–95':<10} {hdr[92:96].hex():<26} {'Version-valid-for number':<32} {u32(hdr,92)}")
    print(f"  {'96–99':<10} {hdr[96:100].hex():<26} {'SQLite version number':<32} {ver}  "
          f"({ver//1_000_000}.{(ver%1_000_000)//1000}.{ver%1000})")

    print(f"\n  Signature check: {'PASS' if magic[:6] == b'SQLite' else 'FAIL'} "
          f"— file begins with {repr(magic[:15].decode('latin-1'))!r}")


# ── Task 4: B-Tree Page Analysis ─────────────────────────────────

def task4_btree_page():
    sep("TASK 4: B-Tree Page Analysis (page 1 B-tree header, bytes 100–119)")

    print("\n  Page 1 is special: its first 100 bytes are the database file header.")
    print("  The B-tree page header for page 1 therefore starts at byte 100.\n")
    print("  Raw xxd dump of bytes 100–119 (B-tree header + start of cell pointers):\n")
    print(xxd(start=100, length=20))

    ph          = read_bytes(100, 8)   # leaf table header is 8 bytes
    page_type   = u8(ph, 0)
    freeblock   = u16(ph, 1)
    num_cells   = u16(ph, 3)
    cell_start  = u16(ph, 5)
    if cell_start == 0:
        cell_start = 65536
    frag_free   = u8(ph, 7)

    hdr       = read_bytes(0, 100)
    page_size = page_size_from_header(hdr)

    # Unallocated gap: from end of cell-pointer array to start of cell content
    ptr_array_end = 100 + 8 + num_cells * 2   # file offset
    free_gap      = cell_start - ptr_array_end

    print("\n  Annotated B-tree page header (file offset 100):")
    print(f"  {'Offset':<10} {'Hex':<8} {'Field':<34} Decoded value")
    print(f"  {'-'*66}")
    print(f"  {'100':<10} {ph[0:1].hex():<8} {'Page type':<34} "
          f"0x{page_type:02X} = {PAGE_TYPES.get(page_type, 'Unknown')}")
    print(f"  {'101–102':<10} {ph[1:3].hex():<8} {'First freeblock offset':<34} "
          f"{freeblock}  {'(0 = no freeblocks)' if freeblock == 0 else ''}")
    print(f"  {'103–104':<10} {ph[3:5].hex():<8} {'Number of cells (records)':<34} {num_cells}")
    print(f"  {'105–106':<10} {ph[5:7].hex():<8} {'Cell content area start':<34} {cell_start}")
    print(f"  {'107':<10} {ph[7:8].hex():<8} {'Fragmented free bytes':<34} {frag_free}")
    print(f"\n  Derived layout for page 1:")
    print(f"    Page size            : {page_size} bytes")
    print(f"    DB file header       : bytes    0 – 99")
    print(f"    B-tree page header   : bytes  100 – 107")
    print(f"    Cell pointer array   : bytes  108 – {108 + num_cells*2 - 1}  ({num_cells} × 2 bytes)")
    print(f"    Unallocated gap      : bytes  {ptr_array_end} – {cell_start - 1}  ({free_gap} bytes)")
    print(f"    Cell content area    : bytes  {cell_start} – {page_size - 1}  "
          f"({page_size - cell_start} bytes used for record payloads)")


# ── Task 5: Cell Pointer Array Examination ────────────────────────

def task5_cell_pointers():
    sep("TASK 5: Cell Pointer Array Examination")

    hdr       = read_bytes(0, 100)
    page_size = page_size_from_header(hdr)

    # ── Page 1 (sqlite_master) ────────────────────────────────────
    ph1        = read_bytes(100, 8)
    nc1        = u16(ph1, 3)
    ptr_start1 = 108
    ptr_data1  = read_bytes(ptr_start1, nc1 * 2)

    print(f"\n  ── Page 1 (sqlite_master root page) ──")
    print(f"  Cell count           : {nc1}")
    print(f"  Cell pointer array   : file bytes {ptr_start1}–{ptr_start1 + nc1*2 - 1}\n")
    print("  Raw xxd of cell pointer array:\n")
    print(xxd(start=ptr_start1, length=nc1 * 2))
    print(f"\n  Decoded pointers  (each value = byte offset within page 1):")
    print(f"  {'#':<6} {'Hex':<10} {'Page offset':<16} {'File offset':<16} Points to")
    print(f"  {'-'*58}")
    for i in range(nc1):
        raw  = ptr_data1[i*2:(i+1)*2]
        offs = struct.unpack(">H", raw)[0]
        print(f"  {i:<6} {raw.hex():<10} {offs:<16} {offs:<16} record {i} in sqlite_master")

    # ── Students table page ───────────────────────────────────────
    conn = sqlite3.connect(DB_FILE)
    cur  = conn.cursor()
    cur.execute("SELECT rootpage FROM sqlite_master WHERE name='students'")
    tbl_rootpage = cur.fetchone()[0]
    conn.close()

    tbl_file_off = (tbl_rootpage - 1) * page_size   # page numbers are 1-based
    tbl_ph       = read_bytes(tbl_file_off, 8)
    nc_tbl       = u16(tbl_ph, 3)
    ptr_start_t  = tbl_file_off + 8                  # no DB header on pages > 1
    ptr_data_t   = read_bytes(ptr_start_t, nc_tbl * 2)

    print(f"\n  ── Page {tbl_rootpage} (students table root page) ──")
    print(f"  File offset of this page : {tbl_file_off}")
    print(f"  Cell count               : {nc_tbl}")
    print(f"  Cell pointer array       : file bytes {ptr_start_t}–{ptr_start_t + nc_tbl*2 - 1}\n")
    print("  Raw xxd of cell pointer array:\n")
    print(xxd(start=ptr_start_t, length=nc_tbl * 2))
    print(f"\n  Decoded pointers (page-relative offsets on page {tbl_rootpage}):")
    print(f"  {'#':<6} {'Hex':<10} {'Page offset':<16} {'File offset':<16} Points to")
    print(f"  {'-'*60}")
    for i in range(nc_tbl):
        raw        = ptr_data_t[i*2:(i+1)*2]
        page_off   = struct.unpack(">H", raw)[0]
        file_off   = tbl_file_off + page_off
        print(f"  {i:<6} {raw.hex():<10} {page_off:<16} {file_off:<16} student record {i}")

    print(f"\n  Key insight: SQLite resolves record i by reading pointer i from")
    print(f"  the array (O(1) index), then jumping directly to that file offset.")


# ── Task 6: Record Storage Analysis ──────────────────────────────

def task6_record_storage():
    sep("TASK 6: Record Storage Analysis")

    with open(DB_FILE, "rb") as f:
        raw = f.read()

    print("\n  Scanning the raw database file for known text values...\n")
    print(f"  {'Search term':<18} {'File offset':<14} {'Page':<8} {'Offset in page'}")
    print(f"  {'-'*54}")

    ps = page_size_from_header(raw[:100])
    targets = [b"Alice Sharma", b"Bob Mehta", b"Clara Singh",
               b"24BCS10001", b"24BCS10005", b"CSE", b"ECE"]
    first_hit = None
    for t in targets:
        pos = raw.find(t)
        if pos != -1:
            pg  = pos // ps + 1
            off = pos % ps
            print(f"  {t.decode()!r:<18} {pos:<14} {pg:<8} {off}")
            if first_hit is None:
                first_hit = pos
        else:
            print(f"  {t.decode()!r:<18} not found")

    if first_hit is not None:
        win_start = max(0, first_hit - 24)
        print(f"\n  64-byte hex window around first record hit (offset {win_start}):\n")
        print(xxd(start=win_start, length=64))

    print("  Observations:")
    print("    • Text fields are stored as raw UTF-8 byte sequences — readable in xxd.")
    print("    • Each cell is prefixed by varint-encoded payload length and row-id.")
    print("    • A varint-encoded header section describes the type/length of each column.")
    print("    • NULL, INTEGER, REAL, TEXT, BLOB each have a distinct serial-type code.")


# ── Task 7: Schema Storage Analysis ──────────────────────────────

def task7_schema_storage():
    sep("TASK 7: Schema Storage Analysis")

    conn = sqlite3.connect(DB_FILE)
    cur  = conn.cursor()
    cur.execute("SELECT type, name, tbl_name, rootpage, sql FROM sqlite_master")
    rows = cur.fetchall()
    conn.close()

    print("\n  sqlite_master records (the internal schema catalog):")
    for i, r in enumerate(rows):
        print(f"\n  Record {i}:")
        print(f"    type      = {r[0]}")
        print(f"    name      = {r[1]}")
        print(f"    tbl_name  = {r[2]}")
        print(f"    rootpage  = {r[3]}")
        print(f"    sql       = {r[4]}")

    with open(DB_FILE, "rb") as f:
        raw = f.read()

    pos = raw.find(b"CREATE TABLE")
    if pos != -1:
        ps  = page_size_from_header(raw[:100])
        print(f"\n  'CREATE TABLE' bytes found at file offset {pos}  "
              f"(page {pos//ps+1}, page-offset {pos%ps})")
        win = max(0, pos - 8)
        print(f"\n  Hex dump of schema record region (offset {win}, 96 bytes):\n")
        print(xxd(start=win, length=96))

    pos2 = raw.find(b"autoindex")
    if pos2 == -1:
        pos2 = raw.find(b"sqlite_autoindex")
    if pos2 != -1:
        print(f"\n  Auto-index name found at file offset {pos2}")
        print(f"\n  Hex dump around auto-index record:\n")
        print(xxd(start=max(0, pos2 - 4), length=48))

    print("  Observation: the full CREATE TABLE SQL text is stored verbatim")
    print("  inside page 1 of the database file, as a record in sqlite_master.")
    print("  SQLite re-reads this on every connection to reconstruct the schema.")


# ── Task 8: Physical File Layout Study ───────────────────────────

def task8_file_layout():
    sep("TASK 8: Physical File Layout Study")

    hdr       = read_bytes(0, 100)
    ps        = page_size_from_header(hdr)
    num_pages = u32(hdr, 28)
    file_size = os.path.getsize(DB_FILE)

    conn = sqlite3.connect(DB_FILE)
    cur  = conn.cursor()
    cur.execute("SELECT type, name, rootpage FROM sqlite_master ORDER BY rootpage")
    objects = cur.fetchall()
    conn.close()

    print(f"\n  File : {DB_FILE}")
    print(f"  Size : {file_size:,} bytes  =  {num_pages} pages × {ps} bytes/page\n")

    print("  Page map:")
    print(f"  {'Page':<8} {'File bytes':<22} {'Content'}")
    print(f"  {'-'*60}")
    # Page 1 always = sqlite_master root
    print(f"  {'1':<8} {'0 – ' + str(ps-1):<22} sqlite_master root  "
          f"(DB header @ 0–99, B-tree @ 100+)")
    # Remaining pages from sqlite_master
    page_labels = {}
    for obj_type, name, rp in objects:
        if rp and rp > 1:
            page_labels[rp] = f"{obj_type} \"{name}\""
    for pg in sorted(page_labels):
        b_start = (pg - 1) * ps
        b_end   = b_start + ps - 1
        print(f"  {pg:<8} {str(b_start)+' – '+str(b_end):<22} {page_labels[pg]}")

    print(f"\n  Physical layout of page 1 (ascii diagram):\n")
    print(f"  ┌{'─'*62}┐")
    print(f"  │  Bytes    0 –  99  │ SQLite Database File Header (100 B)  │")
    print(f"  │  Bytes  100 – 107  │ B-tree Page Header  (8 B, leaf=0x0D) │")
    print(f"  │  Bytes  108 – ...  │ Cell Pointer Array  (2 B × N records)│")
    print(f"  │       . . .        │ Unallocated free space                │")
    print(f"  │  Bytes  ... – end  │ Cell Content Area  (records, growing  │")
    print(f"  │                    │   from high addresses downward)       │")
    print(f"  └{'─'*62}┘")

    print(f"\n  Full file layout:")
    total_cells = 0
    for pg in range(1, num_pages + 1):
        off   = (pg - 1) * ps
        bth   = 100 if pg == 1 else 0   # DB header only on page 1
        ph    = read_bytes(off + bth, 8)
        ptype = u8(ph, 0)
        nc    = u16(ph, 3)
        cs    = u16(ph, 5) or 65536
        label = ("sqlite_master root" if pg == 1
                 else page_labels.get(pg, f"page {pg}"))
        print(f"  Page {pg:<4}  type=0x{ptype:02X} ({PAGE_TYPES.get(ptype,'?'):<26})  "
              f"cells={nc:<4}  free={cs - (bth + 8 + nc*2)} B  [{label}]")
        total_cells += nc

    print(f"\n  Total records across all pages : {total_cells}")
    print(f"\n  Key takeaways:")
    print("    1. SQLite is a single-file database — schema + data + indexes coexist.")
    print("    2. Every component is organized as fixed-size B-tree pages.")
    print("    3. Page 1 always holds the sqlite_master root (schema catalog).")
    print("    4. The 100-byte file header encodes all global database settings.")
    print("    5. Cell content grows downward from the high end of each page,")
    print("       while cell pointers grow upward from byte 8 (or 108 for page 1).")


# ── Main ──────────────────────────────────────────────────────────

def main():
    # Verify xxd is installed
    if subprocess.run(["which", "xxd"], capture_output=True).returncode != 0:
        print("ERROR: 'xxd' not found.")
        print("  macOS  : should be pre-installed. Try: xcode-select --install")
        print("  Linux  : sudo apt install xxd  OR  sudo yum install vim-common")
        sys.exit(1)

    print("=" * 70)
    print("  Lab 4: SQLite3 Internal Structure Analysis Using XXD")
    print("  Student: Talin Daga (24bcs10321)")
    print("=" * 70)

    task1_create_database()
    task2_metadata()
    task3_file_header()
    task4_btree_page()
    task5_cell_pointers()
    task6_record_storage()
    task7_schema_storage()
    task8_file_layout()

    print(f"\n{'='*70}")
    print(f"  Lab 4 Complete")
    print(f"  Database file kept at: {os.path.abspath(DB_FILE)}")
    print(f"  Inspect manually: xxd {DB_FILE} | less")
    print(f"  Open with SQLite: sqlite3 {DB_FILE}")
    print(f"{'='*70}\n")


if __name__ == "__main__":
    main()
