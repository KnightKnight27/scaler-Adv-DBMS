# SSTable on-disk format

An SSTable is an immutable, sorted file produced by flushing a MemTable or by
compaction. All integers are little-endian (single-machine). It is written to a
`.tmp` file and atomically renamed, so a crash never exposes a half-written
table.

```
+-------------------------------------------------------------+
| HEADER (8 bytes)                                            |
|   magic   uint32 = 0x4C534D31 ("LSM1")                      |
|   version uint32 = 1                                        |
+-------------------------------------------------------------+
| DATA BLOCK  (records, strictly ascending by key)           |
|   repeated:                                                 |
|     key   int64                                             |
|     seq   uint64        (version; higher = newer)           |
|     type  uint8         (0 = Put, 1 = Tombstone)            |
|     vlen  uint32        (0 for a Tombstone)                 |
|     value vlen bytes                                        |
+-------------------------------------------------------------+
| SPARSE INDEX  (one entry every 64 data records)            |
|   repeated:                                                 |
|     key       int64                                         |
|     fileOffset uint64   (byte offset of that data record)   |
+-------------------------------------------------------------+
| FOOTER (fixed 52 bytes, written last)                       |
|   indexOffset uint64                                        |
|   indexCount  uint64                                        |
|   count       uint64    (number of data records)           |
|   minKey      int64                                         |
|   maxKey      int64                                         |
|   maxSeq      uint64                                        |
|   magic       uint32 = 0x4C534D31                           |
+-------------------------------------------------------------+
```

**Read path.** Open the file, seek to `end - 52`, read the footer. If the lookup
key is outside `[minKey, maxKey]`, return immediately. Otherwise binary-search
the in-memory sparse index for the largest indexed key <= the lookup key to get a
starting offset, then scan forward (at most 64 records) until the key is found or
passed.

**Write path.** Write the 8-byte header, then data records in ascending key order
(recording a sparse-index entry every 64 records), then the sparse index, then
the footer. Each SSTable holds at most one record per key (it is one sorted
MemTable or one merge), so only cross-file ordering (by `seq`) matters when
several SSTables contain the same key.
