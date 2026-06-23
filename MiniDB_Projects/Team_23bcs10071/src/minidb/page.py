import struct

PAGE_SIZE = 4096
HEADER_SIZE = 4  # num_slots (2B) + free_space_offset (2B)
SLOT_SIZE = 4    # offset (2B) + length (2B)
RECORD_HEADER_SIZE = 8  # xmin (4B) + xmax (4B)

class Page:
    def __init__(self, data=None):
        if data is None:
            self.data = bytearray(PAGE_SIZE)
            self.init_page()
        else:
            assert len(data) == PAGE_SIZE
            self.data = bytearray(data)

    def read_bytes(self, offset, length):
        return bytes(self.data[offset:offset+length])

    def write_bytes(self, offset, bdata):
        self.data[offset:offset+len(bdata)] = bdata

    def clear(self):
        self.data = bytearray(PAGE_SIZE)
        self.init_page()

    def init_page(self):
        struct.pack_into(">HH", self.data, 0, 0, PAGE_SIZE)

    def get_num_slots(self):
        return struct.unpack_from(">H", self.data, 0)[0]

    def set_num_slots(self, num_slots):
        struct.pack_into(">H", self.data, 0, num_slots)

    def get_free_space_offset(self):
        return struct.unpack_from(">H", self.data, 2)[0]

    def set_free_space_offset(self, offset):
        struct.pack_into(">H", self.data, 2, offset)

    def get_slot(self, slot_id):
        num_slots = self.get_num_slots()
        if slot_id < 0 or slot_id >= num_slots:
            return None
        slot_offset = HEADER_SIZE + slot_id * SLOT_SIZE
        offset, length = struct.unpack_from(">HH", self.data, slot_offset)
        return offset, length

    def set_slot(self, slot_id, offset, length):
        slot_offset = HEADER_SIZE + slot_id * SLOT_SIZE
        struct.pack_into(">HH", self.data, slot_offset, offset, length)

    def insert_record(self, record_data, xmin):
        num_slots = self.get_num_slots()
        free_space_offset = self.get_free_space_offset()
        
        slot_start_offset = HEADER_SIZE + num_slots * SLOT_SIZE
        required_record_len = RECORD_HEADER_SIZE + len(record_data)
        
        # Check space: must fit a new slot entry and the record payload
        if free_space_offset - slot_start_offset - SLOT_SIZE < required_record_len:
            return None  # Page is full
        
        new_record_offset = free_space_offset - required_record_len
        
        # Write record metadata: xmin, xmax = 0
        struct.pack_into(">II", self.data, new_record_offset, xmin, 0)
        # Write record payload
        self.data[new_record_offset + RECORD_HEADER_SIZE : new_record_offset + required_record_len] = record_data
        
        # Save slot
        slot_id = num_slots
        self.set_slot(slot_id, new_record_offset, required_record_len)
        
        # Update header
        self.set_num_slots(num_slots + 1)
        self.set_free_space_offset(new_record_offset)
        
        return slot_id

    def delete_record(self, slot_id, xmax):
        slot = self.get_slot(slot_id)
        if slot is None:
            return False
        offset, length = slot
        if length == 0:
            return False
        # Set xmax to denote deletion
        struct.pack_into(">I", self.data, offset + 4, xmax)
        return True

    def get_record(self, slot_id):
        slot = self.get_slot(slot_id)
        if slot is None:
            return None
        offset, length = slot
        if length == 0:
            return None
        xmin, xmax = struct.unpack_from(">II", self.data, offset)
        record_data = bytes(self.data[offset + RECORD_HEADER_SIZE : offset + length])
        return xmin, xmax, record_data

    # Undo a delete (set xmax to 0) during recovery
    def rollback_delete(self, slot_id):
        slot = self.get_slot(slot_id)
        if slot is None:
            return False
        offset, length = slot
        if length == 0:
            return False
        struct.pack_into(">I", self.data, offset + 4, 0)
        return True

    # Undo an insert (mark slot length as 0 / tombstone) during recovery
    def rollback_insert(self, slot_id):
        slot = self.get_slot(slot_id)
        if slot is None:
            return False
        offset, length = slot
        if length == 0:
            return False
        self.set_slot(slot_id, offset, 0) # Clear slot length
        return True

    def write_record_at(self, slot_id, offset, record_data, xmin, xmax=0):
        # Directly write record header (xmin, xmax)
        struct.pack_into(">II", self.data, offset, xmin, xmax)
        # Write record payload
        length = RECORD_HEADER_SIZE + len(record_data)
        self.data[offset + RECORD_HEADER_SIZE : offset + length] = record_data
        # Update slot
        self.set_slot(slot_id, offset, length)
        # Update num_slots
        if slot_id >= self.get_num_slots():
            self.set_num_slots(slot_id + 1)
        # Update free_space_offset
        if offset < self.get_free_space_offset():
            self.set_free_space_offset(offset)

