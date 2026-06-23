import struct

class Schema:
    def __init__(self, columns, types):
        # columns: list of str (e.g. ['id', 'name'])
        # types: list of str (e.g. ['INT', 'VARCHAR(50)'])
        self.columns = columns
        self.types = types

    def pack(self, values):
        if isinstance(values, dict):
            ordered_vals = [values[col] for col in self.columns]
        else:
            ordered_vals = values
        
        packed = bytearray()
        for val, col_type in zip(ordered_vals, self.types):
            if col_type == 'INT':
                packed.extend(struct.pack(">i", int(val)))
            elif col_type.startswith('VARCHAR'):
                # Format: length (2 bytes) + string bytes
                s = str(val).encode('utf-8')
                packed.extend(struct.pack(">H", len(s)))
                packed.extend(s)
        return bytes(packed)

    def unpack(self, byte_data):
        offset = 0
        values = []
        for col_type in self.types:
            if col_type == 'INT':
                val = struct.unpack_from(">i", byte_data, offset)[0]
                offset += 4
                values.append(val)
            elif col_type.startswith('VARCHAR'):
                length = struct.unpack_from(">H", byte_data, offset)[0]
                offset += 2
                val = byte_data[offset:offset+length].decode('utf-8')
                offset += length
                values.append(val)
        return values
