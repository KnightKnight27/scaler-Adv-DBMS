import os
import json
import time
from typing import Dict, Any

class WALManager:
    def __init__(self, data_dir: str):
        self.data_dir = data_dir
        os.makedirs(data_dir, exist_ok=True)
        self.log_path = os.path.join(data_dir, "wal.log")
        self.next_lsn = 1
        
        # Read existing file to determine next LSN
        if os.path.exists(self.log_path):
            with open(self.log_path, "r") as f:
                for line in f:
                    if line.strip():
                        try:
                            record = json.loads(line)
                            self.next_lsn = max(self.next_lsn, record.get("lsn", 0) + 1)
                        except json.JSONDecodeError:
                            pass

        self._log_file = open(self.log_path, "a")

    def append(self, record: Dict[str, Any]) -> int:
        lsn = self.next_lsn
        self.next_lsn += 1
        
        record["lsn"] = lsn
        if "timestamp" not in record or record["timestamp"] is None:
            record["timestamp"] = time.time()
            
        line = json.dumps(record) + "\n"
        self._log_file.write(line)
        return lsn

    def flush(self):
        self._log_file.flush()
        # Force write to physical disk
        try:
            os.fsync(self._log_file.fileno())
        except OSError:
            pass  # fsync might not be supported on all file objects/tests but we try

    def close(self):
        if not self._log_file.closed:
            self.flush()
            self._log_file.close()

    def get_log_records(self) -> list:
        """Reads all records from the log file (for recovery)."""
        self.flush()
        records = []
        if os.path.exists(self.log_path):
            with open(self.log_path, "r") as f:
                for line in f:
                    if line.strip():
                        try:
                            records.append(json.loads(line))
                        except json.JSONDecodeError:
                            pass
        return records
