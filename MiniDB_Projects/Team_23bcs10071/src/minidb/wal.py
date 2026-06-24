import os
import struct
import json
import base64

# Very small WAL: append records as (len, payload)
class WAL:
    def __init__(self, path):
        self.path = path
        # Use a+b to append and read safely
        self.f = open(path, 'ab+')
        self.f.seek(0, os.SEEK_END)

    def append(self, payload: bytes):
        ln = len(payload)
        self.f.write(struct.pack('I', ln))
        self.f.write(payload)
        self.f.flush()

    def close(self):
        self.f.close()

    def log_begin(self, tx_id):
        record = {"type": "BEGIN", "tx_id": tx_id}
        self.append(json.dumps(record).encode('utf-8'))

    def log_commit(self, tx_id):
        record = {"type": "COMMIT", "tx_id": tx_id}
        self.append(json.dumps(record).encode('utf-8'))

    def log_abort(self, tx_id):
        record = {"type": "ABORT", "tx_id": tx_id}
        self.append(json.dumps(record).encode('utf-8'))

    def log_insert(self, tx_id, table_name, page_no, slot_id, offset, data_bytes):
        record = {
            "type": "INSERT",
            "tx_id": tx_id,
            "table": table_name,
            "page_no": page_no,
            "slot_id": slot_id,
            "offset": offset,
            "data": base64.b64encode(data_bytes).decode('utf-8')
        }
        self.append(json.dumps(record).encode('utf-8'))

    def log_delete(self, tx_id, table_name, page_no, slot_id):
        record = {
            "type": "DELETE",
            "tx_id": tx_id,
            "table": table_name,
            "page_no": page_no,
            "slot_id": slot_id
        }
        self.append(json.dumps(record).encode('utf-8'))

    def iter_records(self):
        self.f.flush()
        if not os.path.exists(self.path):
            return
        with open(self.path, 'rb') as f:
            while True:
                header = f.read(4)
                if not header or len(header) < 4:
                    break
                (ln,) = struct.unpack('I', header)
                payload = f.read(ln)
                if len(payload) < ln:
                    break
                try:
                    yield json.loads(payload.decode('utf-8'))
                except Exception:
                    yield payload

