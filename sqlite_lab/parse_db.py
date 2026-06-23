import struct

def read_varint(data, offset):
    val = 0
    bytes_read = 0
    while bytes_read < 8:
        b = data[offset + bytes_read]
        val = (val << 7) | (b & 0x7f)
        bytes_read += 1
        if not (b & 0x80):
            return val, offset + bytes_read
    b = data[offset + 8]
    val = (val << 8) | b
    return val, offset + 9

def parse_value(serial_type, data, offset):
    if serial_type == 0:
        return "NULL (or IPK)", offset
    elif serial_type == 1:
        val = int.from_bytes(data[offset:offset+1], byteorder='big', signed=True)
        return val, offset + 1
    elif serial_type == 2:
        val = int.from_bytes(data[offset:offset+2], byteorder='big', signed=True)
        return val, offset + 2
    elif serial_type == 3:
        val = int.from_bytes(data[offset:offset+3], byteorder='big', signed=True)
        return val, offset + 3
    elif serial_type == 4:
        val = int.from_bytes(data[offset:offset+4], byteorder='big', signed=True)
        return val, offset + 4
    elif serial_type == 5:
        val = int.from_bytes(data[offset:offset+6], byteorder='big', signed=True)
        return val, offset + 6
    elif serial_type == 6:
        val = int.from_bytes(data[offset:offset+8], byteorder='big', signed=True)
        return val, offset + 8
    elif serial_type == 7:
        val = struct.unpack('>d', data[offset:offset+8])[0]
        return val, offset + 8
    elif serial_type == 8:
        return 0, offset
    elif serial_type == 9:
        return 1, offset
    elif serial_type >= 12 and serial_type % 2 == 0:
        length = (serial_type - 12) // 2
        val = data[offset:offset+length]
        return val, offset + length
    elif serial_type >= 13 and serial_type % 2 == 1:
        length = (serial_type - 13) // 2
        val = data[offset:offset+length].decode('utf-8', errors='replace')
        return val, offset + length
    else:
        return f"Unknown Type ({serial_type})", offset

def main():
    with open('lab.db', 'rb') as f:
        db_data = f.read()

    # Database Header is 100 bytes
    print("="*60)
    print("SQLITE3 DATABASE FILE PARSER (LAB EXERCISE)")
    print("="*60)
    print(f"File Size: {len(db_data)} bytes")
    
    # 1. Parse Database Header
    magic = db_data[0:16]
    page_size = int.from_bytes(db_data[16:18], byteorder='big')
    file_change_counter = int.from_bytes(db_data[24:28], byteorder='big')
    db_size_pages = int.from_bytes(db_data[28:32], byteorder='big')
    schema_cookie = int.from_bytes(db_data[40:44], byteorder='big') # wait, schema cookie is 40-43? Let's check: 40 is offset 0x28, so 40-43 is schema cookie
    # Wait, let's verify offset: 0x28 is 40. Yes, 40 to 43.
    encoding = int.from_bytes(db_data[56:60], byteorder='big') # 0x38 is 56.
    sqlite_version = int.from_bytes(db_data[96:100], byteorder='big') # 0x60 is 96.
    
    print("\n--- DATABASE HEADER (First 100 Bytes) ---")
    print(f"Header Magic String:      {magic.decode('ascii', errors='ignore')}")
    print(f"Page Size:                {page_size} bytes")
    print(f"Database Size in Pages:   {db_size_pages} pages")
    print(f"File Change Counter:      {file_change_counter}")
    print(f"Schema Cookie:            {schema_cookie}")
    print(f"Encoding:                 {encoding} (1 = UTF-8, 2 = UTF-16LE, 3 = UTF-16BE)")
    print(f"SQLite Version Number:    {sqlite_version} ({sqlite_version // 1000000}.{(sqlite_version % 1000000) // 1000}.{sqlite_version % 1000})")

    # Let's iterate through the pages
    num_pages = len(db_data) // page_size
    for page_num in range(1, num_pages + 1):
        print("\n" + "="*50)
        print(f"PAGE {page_num} (Offset: { (page_num - 1) * page_size } to { page_num * page_size - 1 })")
        print("="*50)
        
        page_offset = (page_num - 1) * page_size
        
        # B-tree header start
        btree_header_offset = page_offset + (100 if page_num == 1 else 0)
        
        # Page header fields
        page_type = db_data[btree_header_offset]
        first_freeblock = int.from_bytes(db_data[btree_header_offset + 1: btree_header_offset + 3], byteorder='big')
        num_cells = int.from_bytes(db_data[btree_header_offset + 3: btree_header_offset + 5], byteorder='big')
        cell_content_start = int.from_bytes(db_data[btree_header_offset + 5: btree_header_offset + 7], byteorder='big')
        fragmented_bytes = db_data[btree_header_offset + 7]
        
        page_type_str = {
            0x02: "Index Interior Page",
            0x05: "Table Interior Page",
            0x0a: "Index Leaf Page",
            0x0d: "Table Leaf Page"
        }.get(page_type, f"Unknown Page Type ({hex(page_type)})")
        
        print(f"Page B-Tree Type:         {page_type_str} ({hex(page_type)})")
        print(f"First Freeblock Offset:   {first_freeblock}")
        print(f"Number of Cells:          {num_cells}")
        print(f"Cell Content Start:       {cell_content_start} (Page-relative)")
        print(f"Fragmented Free Bytes:    {fragmented_bytes}")
        
        header_len = 8
        if page_type in (0x02, 0x05): # interior pages have right-child page number (4 bytes)
            right_child = int.from_bytes(db_data[btree_header_offset + 8: btree_header_offset + 12], byteorder='big')
            print(f"Right-Child Page Number:  {right_child}")
            header_len = 12
            
        cell_pointer_array_offset = btree_header_offset + header_len
        print(f"Cell Pointer Array Start: {cell_pointer_array_offset - page_offset} (Page-relative)")
        
        # Parse cell pointers
        cell_pointers = []
        for i in range(num_cells):
            ptr_offset = cell_pointer_array_offset + i * 2
            ptr = int.from_bytes(db_data[ptr_offset: ptr_offset + 2], byteorder='big')
            cell_pointers.append(ptr)
        print(f"Cell Pointers:            {cell_pointers}")
        
        # Parse each cell
        for i, ptr in enumerate(cell_pointers):
            cell_abs_offset = page_offset + ptr
            print(f"\n  --- Cell {i+1} at page offset {ptr} (absolute file offset {cell_abs_offset}) ---")
            
            if page_type == 0x0d: # Table Leaf Page
                payload_size, next_offset = read_varint(db_data, cell_abs_offset)
                rowid, next_offset = read_varint(db_data, next_offset)
                print(f"  Payload Size (Varint):   {payload_size} bytes")
                print(f"  RowID / Key (Varint):    {rowid}")
                
                # Payload parsing (Record format)
                payload_start = next_offset
                record_header_size, next_offset = read_varint(db_data, payload_start)
                print(f"  Record Header Size:      {record_header_size} bytes")
                
                # Read serial types
                serial_types = []
                current_hdr_offset = next_offset
                header_end = payload_start + record_header_size
                while current_hdr_offset < header_end:
                    stype, current_hdr_offset = read_varint(db_data, current_hdr_offset)
                    serial_types.append(stype)
                print(f"  Serial Types:            {serial_types}")
                
                # Read values
                val_offset = header_end
                for j, stype in enumerate(serial_types):
                    val, val_offset = parse_value(stype, db_data, val_offset)
                    # If this is column 0 and type is INTEGER PRIMARY KEY, it parses as NULL, 
                    # but actually maps to the rowid.
                    if j == 0 and stype == 0:
                        val = f"{val} [Mapped to RowID {rowid}]"
                    print(f"    Col {j} (Serial Type {stype}): {val}")
                    
            elif page_type == 0x05: # Table Interior Page
                left_child = int.from_bytes(db_data[cell_abs_offset : cell_abs_offset + 4], byteorder='big')
                rowid, next_offset = read_varint(db_data, cell_abs_offset + 4)
                print(f"  Left Child Page:         {left_child}")
                print(f"  Key / RowID (Varint):    {rowid}")
                
            else:
                print("  Cell parsing not implemented for this page type.")

if __name__ == '__main__':
    main()
