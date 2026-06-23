# RocksDB

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Architecture
RocksDB is an embeddable key-value storage engine based on Log-Structured Merge Trees. It is optimized for high write throughput by converting random writes into mostly sequential appends.

## Write Path
1. A write enters the WAL for durability.
2. The key-value pair is inserted into an in-memory memtable.
3. When the memtable fills, it becomes immutable.
4. The immutable memtable is flushed to an SSTable file.
5. Background compaction merges SSTables across levels.

## Read Path
Reads may check the memtable, immutable memtables, block cache, bloom filters, and SSTables. Bloom filters reduce unnecessary disk reads for missing keys.

## Compaction
Compaction keeps read amplification under control by merging sorted files and discarding overwritten or deleted keys. However, compaction also creates write amplification because data may be rewritten across levels.

## Tuning Points
- Memtable size controls flush frequency.
- Bloom filters help point lookups.
- Block cache improves repeated reads.
- Level size multiplier affects write amplification.
- Compression reduces disk usage but adds CPU work.

## Key Takeaway
RocksDB is a strong fit for write-heavy key-value workloads. Its main design tradeoff is balancing write amplification, read amplification, and space amplification.
