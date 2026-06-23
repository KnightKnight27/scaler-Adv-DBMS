import struct

class Page:
    PAGE_SIZE = 4096
    
    # Header format:
    # page_id (4 bytes), lsn (8 bytes), num_slots (2 bytes), free_space_offset (2 bytes), next_page_id (4 bytes)
    HEADER_FORMAT = ">IQHHI"  # 4 + 8 + 2 + 2 + 4 = 20 bytes
    HEADER_SIZE = 20
    
    # Slot format: offset (2 bytes), length (2 bytes), flags (1 byte: 1 = active, 0 = deleted)
    SLOT_FORMAT = ">HHB"
    SLOT_SIZE = 5

    def __init__(self, page_id=0, data=None):
        self.data = bytearray(data) if data else bytearray(self.PAGE_SIZE)
        if data is None:
            self.set_page_id(page_id)
            self.set_lsn(0)
            self.set_num_slots(0)
            self.set_free_space_offset(self.PAGE_SIZE)
            self.set_next_page_id(0xFFFFFFFF)  # Invalid next page ID initially

    def get_page_id(self):
        return struct.unpack_from(">I", self.data, 0)[0]

    def set_page_id(self, page_id):
        struct.pack_into(">I", self.data, 0, page_id)

    def get_lsn(self):
        return struct.unpack_from(">Q", self.data, 4)[0]

    def set_lsn(self, lsn):
        struct.pack_into(">Q", self.data, 4, lsn)

    def get_num_slots(self):
        return struct.unpack_from(">H", self.data, 12)[0]

    def set_num_slots(self, num):
        struct.pack_into(">H", self.data, 12, num)

    def get_free_space_offset(self):
        return struct.unpack_from(">H", self.data, 14)[0]

    def set_free_space_offset(self, offset):
        struct.pack_into(">H", self.data, 14, offset)

    def get_next_page_id(self):
        return struct.unpack_from(">I", self.data, 16)[0]

    def set_next_page_id(self, next_page_id):
        struct.pack_into(">I", self.data, 16, next_page_id)

    # Slotted Page operations
    def get_slot(self, slot_idx):
        slot_offset = self.HEADER_SIZE + slot_idx * self.SLOT_SIZE
        if slot_offset + self.SLOT_SIZE > self.get_free_space_offset():
            return None
        return struct.unpack_from(self.SLOT_FORMAT, self.data, slot_offset)

    def set_slot(self, slot_idx, offset, length, flags):
        slot_offset = self.HEADER_SIZE + slot_idx * self.SLOT_SIZE
        struct.pack_into(self.SLOT_FORMAT, self.data, slot_offset, offset, length, flags)

    def has_enough_space(self, record_len):
        num_slots = self.get_num_slots()
        free_space_offset = self.get_free_space_offset()
        # Find if we can reuse a deleted slot
        reusable_slot = -1
        for i in range(num_slots):
            _, _, flag = self.get_slot(i)
            if flag == 0:
                reusable_slot = i
                break
        
        slot_needed = 0 if reusable_slot != -1 else self.SLOT_SIZE
        available = free_space_offset - (self.HEADER_SIZE + num_slots * self.SLOT_SIZE)
        return available >= (record_len + slot_needed)

    def insert_record(self, xmin, xmax, payload_bytes):
        # Record Header: xmin (4 bytes), xmax (4 bytes)
        record_data = struct.pack(">II", xmin, xmax) + payload_bytes
        record_len = len(record_data)

        if not self.has_enough_space(record_len):
            return -1

        num_slots = self.get_num_slots()
        free_space_offset = self.get_free_space_offset()

        # Find if we can reuse a deleted slot
        slot_idx = -1
        for i in range(num_slots):
            _, _, flag = self.get_slot(i)
            if flag == 0:
                slot_idx = i
                break

        new_offset = free_space_offset - record_len
        self.data[new_offset:new_offset+record_len] = record_data
        self.set_free_space_offset(new_offset)

        if slot_idx != -1:
            # Reuse slot
            self.set_slot(slot_idx, new_offset, record_len, 1)
        else:
            # Create new slot
            slot_idx = num_slots
            self.set_slot(slot_idx, new_offset, record_len, 1)
            self.set_num_slots(num_slots + 1)

        return slot_idx

    def get_record(self, slot_idx):
        num_slots = self.get_num_slots()
        if slot_idx < 0 or slot_idx >= num_slots:
            return None
        
        slot = self.get_slot(slot_idx)
        if not slot:
            return None
        
        offset, length, flag = slot
        if flag == 0:
            return None  # Deleted slot
        
        record_bytes = self.data[offset:offset+length]
        if len(record_bytes) < 8:
            return None
        
        xmin, xmax = struct.unpack_from(">II", record_bytes, 0)
        payload = record_bytes[8:]
        return xmin, xmax, payload

    def delete_record(self, slot_idx, xmax=0):
        num_slots = self.get_num_slots()
        if slot_idx < 0 or slot_idx >= num_slots:
            return False
        
        slot = self.get_slot(slot_idx)
        if not slot:
            return False
        
        offset, length, flag = slot
        if flag == 0:
            return False  # Already deleted
        
        if xmax > 0:
            # MVCC soft delete: update xmax inside the record data in-place
            struct.pack_into(">I", self.data, offset + 4, xmax)
        else:
            # 2PL hard delete: update slot flag to 0
            self.set_slot(slot_idx, offset, length, 0)
            
        return True

    def update_record(self, slot_idx, xmin, xmax, payload_bytes):
        num_slots = self.get_num_slots()
        if slot_idx < 0 or slot_idx >= num_slots:
            return False
            
        slot = self.get_slot(slot_idx)
        if not slot:
            return False
            
        offset, length, flag = slot
        new_record_data = struct.pack(">II", xmin, xmax) + payload_bytes
        if len(new_record_data) == length:
            self.data[offset:offset+length] = new_record_data
            return True
        else:
            self.delete_record(slot_idx, xmax=xmax)
            return self.insert_record(xmin, 0, payload_bytes)
