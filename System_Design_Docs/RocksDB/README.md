# RocksDB Architecture

## 1. Problem Background

RocksDB was developed by Facebook (2012) to solve a specific problem: **write-heavy workloads at scale**. Traditional B-tree databases optimize for read performance, but RocksDB trades off read complexity for write throughput.

**Key Use Cases**:
- Time-series databases (metrics collection)
- Cache systems (LRU eviction, replacement)
- Log storage and analysis
- Change Data Capture (CDC) systems

## 2. Architecture Overview

LSM Tree (Log-Structured Merge):

```
Write Path (Sequential):
Application → MemTable → Immutable MemTable → L0 (SSTable) → Compaction → L1, L2, ... Ln

Read Path (Multiple checks):
Application → MemTable → Immutable MemTables → L0 (with Bloom filter) → L1+ (with Bloom filter) → Result
```

**Key Insight**: Writes batch in MemTable (fast), reads check multiple levels (slower but acceptable for write-heavy workloads).

## 3. Internal Design

### MemTable

In-memory sorted buffer for incoming writes. When full (~64 MB default), flushed to disk as SSTable.

- **Operations**: PUT, GET, DELETE (all O(log n))
- **Flush**: Sequential write to disk (fast)
- **Benefit**: Batches writes, amortizes disk I/O

### Immutable MemTable

MemTable full but not yet flushed. Multiple immutable MemTables can exist, enabling pipelining (write to new MemTable while flushing old).

### SSTable (Sorted String Table)

Immutable on-disk storage of key-value pairs, organized in levels (L0, L1, ..., Ln).

**Structure**:
```
SSTable:
├─ Data Blocks (compressed key-value pairs)
├─ Index Block (pointers for binary search)
├─ Bloom Filter Block (fast rejection of non-existent keys)
└─ Footer (metadata)
```

### Compaction

Process of merging SSTables from lower levels to higher levels.

**Leveled Compaction** (default):
- Strict size ratios between levels
- L0 compact to L1 when L0 exceeds threshold
- Result: Bounded read amplification (~10 table checks per read)
- Cost: High write amplification (data rewritten multiple times)

### Bloom Filters

Per-SSTable probabilistic data structure for fast key rejection.

- **False Positives Allowed**: "Key might be in this table" (must check)
- **False Negatives Not Allowed**: "Key definitely not in this table" (skip file)
- **Benefit**: Eliminates 99% of unnecessary reads (~1% false positive rate)

## 4. Design Trade-Offs

### LSM vs B-Tree

**RocksDB (LSM)**:
- ✓ Sequential writes (high throughput)
- ✓ Batched I/O (efficient)
- ✗ Multiple levels to check (slower reads)
- ✗ Write amplification (data rewritten during compaction)

**B-Tree Databases**:
- ✓ Single binary search (fast reads)
- ✗ Random disk writes (low throughput)

**Conclusion**: Choose based on workload (write-heavy → LSM, read-heavy → B-tree).

### Write Amplification vs Read Amplification

**Leveled Compaction** (RocksDB choice):
- Write amplification: 7-10x (acceptable for write-heavy)
- Read amplification: ~10 table checks (mitigated by Bloom filters)

**Tiered Compaction** (alternative):
- Write amplification: 2-3x
- Read amplification: 100+ table checks (slow)

**Trade-off**: RocksDB sacrifices write amplification for bounded read performance.

### Space Amplification

Deleted data occupies space until compaction reclaims it.

- Typical: 10-20% overhead
- Mitigated by: Periodic full compaction, monitoring

## 5. Experiments & Observations

### Write Throughput

RocksDB achieves 50,000-100,000 writes/sec (vs ~200 writes/sec for B-tree random writes).

### Bloom Filter Effectiveness

Bloom filters reduce negative lookups by ~99% (1% false positive rate).

```sql
-- RocksDB statistics
db.GetProperty("rocksdb.stats") → Shows:
- Compaction stats
- I/O counts
- Bloom filter hits/misses
```

### Compaction Overhead

Compaction CPU spikes when L0 threshold exceeded; can reduce write throughput temporarily.

## 6. Key Learnings

1. **LSM trees optimize for sequential write throughput**: MemTable batching + compaction enables high write rates.

2. **Read complexity is acceptable trade-off**: Multiple levels + Bloom filters mitigate read cost for write-heavy workloads.

3. **Write amplification is intentional**: Data rewritten during compaction, but throughput benefit worth the extra I/O.

4. **Bloom filters dramatically improve read performance**: ~1% memory overhead eliminates 99% of unnecessary disk reads.

5. **Compaction is critical to performance**: Leveled compaction provides bounded read amplification but requires careful tuning.

6. **RocksDB targets specific use cases**: Excellent for time-series, caches, and log storage; not ideal for read-heavy OLTP.

7. **Recovery is fast**: Bounded by MemTable size, not total dataset size (unlike B-tree WAL replay).

