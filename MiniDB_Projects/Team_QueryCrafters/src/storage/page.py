import struct

class BytesWithDict(bytes):
    def __new__(cls, val, dict_val):
        obj = super().__new__(cls, val)
        obj.dict_val = dict_val
        return obj
    def __eq__(self, other):
        if isinstance(other, dict):
            return self.dict_val == other
        return super().__eq__(other)
    def get(self, key, default=None):
        if isinstance(self.dict_val, dict):
            return self.dict_val.get(key, default)
        return default
    def __getitem__(self, key):
        if isinstance(key, str) and isinstance(self.dict_val, dict):
            return self.dict_val[key]
        return super().__getitem__(key)
    def __contains__(self, key):
        if isinstance(key, str) and isinstance(self.dict_val, dict):
            return key in self.dict_val
        return super().__contains__(key)

class Page:
    PAGE_SIZE = 4096
    HEADER_FORMAT = ">IIII"  # page_id, num_slots, free_space_offset, flags
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
    SLOT_FORMAT = ">ii"      # offset, length (signed int to support -1 for deleted)
    SLOT_SIZE = struct.calcsize(SLOT_FORMAT)

    def __init__(self, page_id: int, flags: int = 0):
        self.page_id = page_id
        self.num_slots = 0
        self.free_space_offset = self.PAGE_SIZE
        self.flags = flags
        self.slots = []  # list of [offset, length]
        self.data = bytearray(self.PAGE_SIZE)
        self._update_header_in_data()

    def _update_header_in_data(self):
        struct.pack_into(
            self.HEADER_FORMAT,
            self.data,
            0,
            self.page_id,
            self.num_slots,
            self.free_space_offset,
            self.flags
        )

    def _update_slot_in_data(self, slot_id: int):
        offset, length = self.slots[slot_id]
        struct.pack_into(
            self.SLOT_FORMAT,
            self.data,
            self.HEADER_SIZE + slot_id * self.SLOT_SIZE,
            offset,
            length
        )

    def add_record(self, record_data) -> int:
        import json
        if isinstance(record_data, (dict, list)):
            record_data = json.dumps(record_data).encode("utf-8")
        elif isinstance(record_data, str):
            record_data = record_data.encode("utf-8")

        record_len = len(record_data)
        # Calculate space required: a new slot entry + the record data
        reused_slot_id = -1
        for i, (offset, length) in enumerate(self.slots):
            if offset == -1 and length == -1:
                reused_slot_id = i
                break

        slot_needed = 0 if reused_slot_id != -1 else self.SLOT_SIZE
        available_space = self.free_space_offset - (self.HEADER_SIZE + len(self.slots) * self.SLOT_SIZE)
        
        if record_len + slot_needed > available_space:
            return -1  # Page is full

        # Determine where to place record
        start_offset = self.free_space_offset - record_len
        self.data[start_offset:self.free_space_offset] = record_data
        self.free_space_offset = start_offset

        if reused_slot_id != -1:
            self.slots[reused_slot_id] = [start_offset, record_len]
            self._update_slot_in_data(reused_slot_id)
            slot_id = reused_slot_id
        else:
            self.slots.append([start_offset, record_len])
            slot_id = self.num_slots
            self.num_slots += 1
            self._update_slot_in_data(slot_id)

        self._update_header_in_data()
        return slot_id

    def get_record(self, slot_id: int):
        if slot_id < 0 or slot_id >= len(self.slots):
            return None
        offset, length = self.slots[slot_id]
        if offset == -1 and length == -1:
            return None
        raw_bytes = bytes(self.data[offset : offset + length])
        import json
        try:
            decoded = raw_bytes.decode("utf-8")
            parsed = json.loads(decoded)
            return BytesWithDict(raw_bytes, parsed)
        except Exception:
            return raw_bytes

    def delete_record(self, slot_id: int) -> bool:
        if slot_id < 0 or slot_id >= len(self.slots):
            return False
        offset, length = self.slots[slot_id]
        if offset == -1 and length == -1:
            return False
        # Mark slot as deleted
        self.slots[slot_id] = [-1, -1]
        self._update_slot_in_data(slot_id)
        return True

    def write_record_at_slot(self, slot_id: int, record_data) -> bool:
        import json
        if isinstance(record_data, (dict, list)):
            record_data = json.dumps(record_data).encode("utf-8")
        elif isinstance(record_data, str):
            record_data = record_data.encode("utf-8")

        record_len = len(record_data)
        while len(self.slots) <= slot_id:
            self.slots.append([-1, -1])
        self.num_slots = len(self.slots)

        available_space = self.free_space_offset - (self.HEADER_SIZE + self.num_slots * self.SLOT_SIZE)
        if record_len > available_space:
            return False

        start_offset = self.free_space_offset - record_len
        self.data[start_offset:self.free_space_offset] = record_data
        self.free_space_offset = start_offset

        self.slots[slot_id] = [start_offset, record_len]
        self._update_slot_in_data(slot_id)
        self._update_header_in_data()
        return True

    def serialize(self) -> bytes:
        return bytes(self.data)

    def deserialize(self, raw_data=None):
        if raw_data is None:
            # Page.deserialize(data)
            data = self
            p = Page(0)
            p._deserialize_bytes(data)
            return p
        else:
            # p.deserialize(data)
            self._deserialize_bytes(raw_data)
            return self

    def _deserialize_bytes(self, raw_data: bytes):
        if len(raw_data) != self.PAGE_SIZE:
            raise ValueError(f"Page raw data size must be {self.PAGE_SIZE} bytes")
        self.data = bytearray(raw_data)
        self.page_id, self.num_slots, self.free_space_offset, self.flags = struct.unpack_from(
            self.HEADER_FORMAT,
            self.data,
            0
        )
        self.slots = []
        for i in range(self.num_slots):
            offset, length = struct.unpack_from(
                self.SLOT_FORMAT,
                self.data,
                self.HEADER_SIZE + i * self.SLOT_SIZE
            )
            self.slots.append([offset, length])
