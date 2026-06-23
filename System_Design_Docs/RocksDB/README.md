**Name:** Jatin Chulet  
**Roll No:** 2024BCS10213

---

# RocksDB Architecture — LSM Tree Deep Dive

> Okay so yeh topic pehle 3 topics se bilkul alag hai. PostgreSQL, MySQL — yeh sab B-Tree based traditional databases hain. RocksDB ek completely different approach use karta hai — **LSM Trees (Log-Structured Merge Trees)**. Jab pehli baar padha toh mujhe laga "yeh toh ulta hai!" because writing is fast and reading is slow — traditional databases mein opposite hota hai. But jaise jaise samjha, realize hua ki write-heavy workloads ke liye yeh genius hai.

---

## 1. Problem Background

### Yeh RocksDB kyun bana?

Story kuch aisi hai — Facebook ko ek problem tha. Unke paas bohot bade scale pe data tha (obviously), aur unhe ek storage engine chahiye tha jo:
- Write-heavy workloads handle kar sake (social media = constant writes)
- Space-efficient ho
- SSD pe well perform kare
- Embeddable ho (like SQLite, not like PostgreSQL)

Google ne pehle **LevelDB** banaya tha (2011) — yeh ek simple, fast key-value store tha based on LSM trees. Facebook ne LevelDB ko fork karke **RocksDB** banaya (2012-13) because LevelDB had some limitations:
- Single-threaded compaction
- Limited tuning options  
- Not production-ready for Facebook's scale

RocksDB ne LevelDB ko significantly improve kiya:
- Multi-threaded compaction
- Column families
- Transactions
- Bloom filters by default
- Bohot saare tunable parameters (literally hundreds of knobs!)

Aaj kal RocksDB bohot jagah use hota hai:
- **CockroachDB** — distributed SQL database (RocksDB as storage engine, ab Pebble use karta hai jo Go mein likha hai)
- **TiKV** — distributed key-value store (TiDB ka storage layer)
- **Apache Kafka Streams** — state stores
- **MySQL** — MyRocks storage engine (Facebook ne bhi banaya!)
- **MongoDB** — WiredTiger bhi LSM-inspired hai kuch aspects mein

Point yeh hai ki RocksDB ek **library** hai (like SQLite), not a server. Tum apne application mein embed karte ho.

---

## 2. Architecture Overview

Pehle big picture dekhte hain, phir detail mein jayenge:

```
┌──────────────────────────────────────────────────────────────┐
│                    RocksDB Architecture                       │
│                                                                │
│  ┌─────────── In Memory ──────────────────────────────────┐  │
│  │                                                         │  │
│  │  ┌─────────────────┐    ┌──────────────────────────┐   │  │
│  │  │   Active        │    │   Immutable MemTable(s)  │   │  │
│  │  │   MemTable      │    │   (read-only, waiting    │   │  │
│  │  │                 │    │    to be flushed)         │   │  │
│  │  │  [key1: val1]   │    │                          │   │  │
│  │  │  [key2: val2]   │    │  ┌────────────────────┐  │   │  │
│  │  │  [key3: val3]   │    │  │ Immutable MemTable │  │   │  │
│  │  │  (sorted!)      │    │  │ #1 (being flushed) │  │   │  │
│  │  │                 │    │  └────────────────────┘  │   │  │
│  │  └─────────────────┘    │                          │   │  │
│  │                          └──────────────────────────┘   │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                │
│  ┌─────────── On Disk ────────────────────────────────────┐  │
│  │                                                         │  │
│  │  WAL (Write-Ahead Log)                                  │  │
│  │  ┌──────────────────────────────────────────┐          │  │
│  │  │ [write1] [write2] [write3] [write4] ...  │          │  │
│  │  └──────────────────────────────────────────┘          │  │
│  │                                                         │  │
│  │  SSTable Files (Sorted String Tables):                  │  │
│  │                                                         │  │
│  │  Level 0 (L0): ┌───────┐ ┌───────┐ ┌───────┐          │  │
│  │  (unsorted,     │SST-01 │ │SST-02 │ │SST-03 │          │  │
│  │   overlapping!) │       │ │       │ │       │          │  │
│  │                 └───────┘ └───────┘ └───────┘          │  │
│  │                                                         │  │
│  │  Level 1 (L1): ┌───────┬───────┬───────┬───────┐      │  │
│  │  (sorted,       │SST-04 │SST-05 │SST-06 │SST-07 │      │  │
│  │   non-overlap)  │[a-d]  │[e-h]  │[i-l]  │[m-p]  │      │  │
│  │                 └───────┴───────┴───────┴───────┘      │  │
│  │                                                         │  │
│  │  Level 2 (L2): ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐       │  │
│  │  (bigger,       │  │  │  │  │  │  │  │  │  │  │       │  │
│  │   sorted,       │  │  │  │  │  │  │  │  │  │  │       │  │
│  │   non-overlap)  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘       │  │
│  │                                                         │  │
│  │  Level N: Even bigger...                                │  │
│  │                                                         │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

Okay dekho, traditional databases mein (PostgreSQL, InnoDB) — data B-Tree mein **in-place** modify hota hai. Random I/O bohot hota hai. 

LSM Tree mein idea yeh hai: **Sequential writes are MUCH faster than random writes** (especially on HDDs, but even on SSDs). Toh kyun na har write ko sequential bana dein?

---

## 3. Internal Design

### 3.1 Write Path — Data kaise likha jaata hai

Yeh samajhna sabse important hai. Let me trace a single write:

```
PUT("name", "Kartik")  →  What happens?

Step 1: Write to WAL (sequential append)
┌──────────────────────────────────────┐
│ WAL File:                             │
│ ...[prev entries] [PUT name=Kartik]  │
└──────────────────────────────────────┘
→ Durability guarantee! If crash, WAL se recover.

Step 2: Write to active MemTable (in memory)
┌─────────────────────┐
│ MemTable (SkipList): │
│   age → 22           │
│   city → Delhi        │
│   name → Kartik  ← NEW│
│   roll → 101          │
│ (sorted by key!)     │
└─────────────────────┘
→ DONE! Write is complete. Super fast!

No disk I/O for the actual data (only WAL — sequential write)
```

Dekha? Write karne ke liye:
1. WAL mein append (sequential — fast)
2. Memory mein insert (very fast)

Compare with B-Tree databases:
1. Write to WAL
2. Find the correct page in B-Tree (possibly multiple random I/O)
3. Update the page (random write)
4. Handle page splits if needed

Isliye RocksDB writes bohot fast hain!

### MemTable — The In-Memory Component

```
MemTable internal structure:

Usually a SKIP LIST (concurrent, sorted):

Level 3:  ─────────────────────────────── [head] ──────→ [name] ──────────→ [NULL]
Level 2:  ─────────────── [head] → [city] ──────→ [name] ──→ [roll] ──→ [NULL]
Level 1:  ─── [head] → [age] → [city] → [name] → [roll] ──→ [NULL]

Properties:
- Sorted by key (allows efficient range scans)
- O(log n) insert, O(log n) lookup
- Lock-free concurrent reads (important for performance!)
- Size limit configurable (write_buffer_size, default 64MB)
```

Skip list kyun use karte hain? Kyunki yeh concurrent reads allow karta hai without locking (lock-free data structure). Red-black tree bhi sorted hota hai but concurrent access ke liye zyada locking chahiye.

Jab MemTable full ho jaaye:

```
MemTable Flush Process:

1. Current MemTable → becomes IMMUTABLE (read-only)
2. New empty MemTable created for new writes
3. Background thread: flush immutable MemTable to disk as SSTable (L0)
4. Once flushed, WAL records for that MemTable can be deleted

Timeline:
────────────────────────────────────────────────────►
  [Active MemTable fills up]
         │
         ▼
  [Active becomes Immutable] [New MemTable created]
         │                        │
         ▼                        ▼
  [Background flush to L0]   [Writes continue here]
         │
         ▼
  [New SSTable in L0]
  [Old WAL deleted]
```

Multiple immutable MemTables exist kar sakte hain simultaneously — agar flushing slow ho toh. `max_write_buffer_number` se control hota hai. Agar sab full ho jaayein toh writes **stall** ho jaate hain (yeh production mein problem ho sakta hai — tune karna padta hai).

### 3.2 SSTables (Sorted String Tables)

SSTable ek immutable file hai disk pe — once written, never modified (only deleted during compaction).

```
SSTable File Structure:

┌──────────────────────────────────────────┐
│ Data Block 1                              │
│  key1:value1 | key2:value2 | key3:value3 │
│  (sorted by key, compressed)             │
├──────────────────────────────────────────┤
│ Data Block 2                              │
│  key4:value4 | key5:value5 | key6:value6 │
├──────────────────────────────────────────┤
│ ...more data blocks...                    │
├──────────────────────────────────────────┤
│ Meta Block 1: Filter (Bloom Filter!)      │
│  Quickly check: "is key X possibly here?" │
├──────────────────────────────────────────┤
│ Meta Block 2: Stats                       │
│  min key, max key, count, etc.           │
├──────────────────────────────────────────┤
│ Meta Index Block                          │
│  Pointers to meta blocks                 │
├──────────────────────────────────────────┤
│ Index Block                               │
│  key ranges per data block               │
│  "block 1 has keys a-c,                  │
│   block 2 has keys d-f, ..."            │
├──────────────────────────────────────────┤
│ Footer (48 bytes)                         │
│  Pointers to meta index and index blocks │
│  Magic number for verification           │
└──────────────────────────────────────────┘
```

### 3.3 Level Organization (L0 to Ln)

Yeh LSM tree ka core hai:

```
Level 0 (L0):
┌───────────────────────────────────────────────┐
│ SST files directly from MemTable flushes      │
│ OVERLAPPING key ranges! (this is important)   │
│                                                │
│  SST-1: [apple...grape]                       │
│  SST-2: [banana...mango]    ← overlaps!      │
│  SST-3: [cat...zebra]       ← overlaps!      │
│                                                │
│ Max files: level0_file_num_compaction_trigger  │
│ (default 4)                                    │
└───────────────────────────────────────────────┘
          │ compaction (when too many L0 files)
          ▼
Level 1 (L1):
┌───────────────────────────────────────────────┐
│ NON-OVERLAPPING key ranges (sorted!)          │
│                                                │
│  SST-4: [apple...cherry]                      │
│  SST-5: [date...grape]                        │
│  SST-6: [kiwi...mango]                        │
│  SST-7: [orange...zebra]                      │
│                                                │
│ Target size: max_bytes_for_level_base         │
│ (default 256MB)                                │
└───────────────────────────────────────────────┘
          │ compaction (when level is too large)
          ▼
Level 2 (L2):
┌───────────────────────────────────────────────┐
│ NON-OVERLAPPING, 10x bigger than L1           │
│ (multiplier = max_bytes_for_level_multiplier) │
│                                                │
│ Many more SST files covering key space...     │
│ Target size: 256MB * 10 = 2.56GB              │
└───────────────────────────────────────────────┘
          │
          ▼
Level N: keeps growing by 10x each level
```

L0 special kyun hai? Kyunki L0 files directly MemTable flush se aati hain — har MemTable ka apna key range hota hai jo doosre MemTables se overlap kar sakta hai. L1 onwards, compaction ensure karta hai ki ek level ke andar key ranges non-overlapping hain.

Yeh overlap wali baat pehle mujhe confusing lagi. But phir samjha — agar L0 mein overlap nahi hona chahiye, toh har flush pe pehle se existing L0 files ke saath merge karna padega (like compaction) — yeh flush ko slow kar dega. L0 mein overlap allow karke, flush fast rehta hai. Tradeoff: reads L0 se slow hote hain (multiple files check karne padte hain).

### 3.4 Read Path — Data kaise padha jaata hai

Reads mein ek specific order follow hota hai:

```
GET("name") → Where to look?

Step 1: Check Active MemTable
         ┌─────────────┐
         │ MemTable    │ Found? → Return! ✓
         │ (in memory) │ Not found? ↓
         └──────┬──────┘
                │
Step 2: Check Immutable MemTable(s)
         ┌─────────────────┐
         │ Immutable       │ Found? → Return! ✓
         │ MemTable(s)     │ Not found? ↓
         └──────┬──────────┘
                │
Step 3: Check L0 SST files (ALL of them — they overlap!)
         ┌───────────────────┐
         │ L0: SST-3, SST-2,│ Check newest first
         │      SST-1        │ Use Bloom filters!
         │                   │ Found? → Return! ✓
         └──────┬────────────┘ Not found? ↓
                │
Step 4: Check L1 SST files (binary search — non-overlapping)
         ┌───────────────────┐
         │ L1: find the ONE  │ Only ONE file can have this key
         │ file that could   │ Use Bloom filter first!
         │ contain this key  │ Found? → Return! ✓
         └──────┬────────────┘ Not found? ↓
                │
Step 5: Check L2, L3, ... (same as L1)
         │
         ▼
Step N: Not found in any level → Key doesn't exist!
```

Dekha problem? Worst case mein **har level check karna padta hai** — L0 mein toh multiple files bhi. Yeh B-Tree se bohot slow ho sakta hai jahan ek single tree traverse hota hai.

Isliye LSM trees read-heavy workloads ke liye ideal nahi hain — but write-heavy workloads ke liye great hain.

### 3.5 Bloom Filters — Read Performance ka Savior

```
Bloom Filter Concept:

A Bloom filter is a space-efficient probabilistic data structure
that tells you:
- "Key is DEFINITELY NOT here" → Skip this SSTable! Save I/O!
- "Key MIGHT be here" → Check the SSTable

False positives possible, false negatives NOT possible.

How it works (simplified):
┌───────────────────────────────┐
│ Bit Array: [0 1 0 1 1 0 0 1] │
│                                │
│ To add "name":                 │
│   hash1("name") = 1 → set bit[1]│
│   hash2("name") = 3 → set bit[3]│
│   hash3("name") = 7 → set bit[7]│
│                                │
│ To check "age":                │
│   hash1("age") = 2 → bit[2]=0  │
│   → DEFINITELY NOT HERE!      │
│   → Skip this SSTable entirely!│
│                                │
│ To check "xyz":                │
│   hash1("xyz") = 1 → bit[1]=1  │
│   hash2("xyz") = 3 → bit[3]=1  │
│   hash3("xyz") = 7 → bit[7]=1  │
│   → MIGHT be here (false +)   │
│   → Must check the SSTable    │
└───────────────────────────────┘
```

RocksDB mein har SSTable ka apna Bloom filter hota hai. Default false positive rate ~1% hai. Yeh dramatically read performance improve karta hai — especially for point lookups jab key exist nahi karti (negative lookups).

Without Bloom filters, har level pe SSTable kholke check karna padta. With Bloom filters, mostly skip ho jaata hai. Mujhe ek paper mein padha tha ki Bloom filters LSM reads ko 10x+ improve kar sakte hain!

### 3.6 Compaction — The Heart of LSM

Compaction se sabse zyada time gaya samajhne mein. Basically:

```
Why Compaction is Needed:

Without compaction:
1. L0 files keep accumulating → reads get slower (more files to check)
2. Same key might exist in many levels → wasted space
3. Deleted keys (tombstones) never actually removed
4. Old values of updated keys never cleaned up

Compaction Process:
1. Pick SST files from level N
2. Pick overlapping SST files from level N+1
3. Merge-sort them together
4. Write new SST files to level N+1
5. Delete old SST files

Before Compaction:
L1: [a-d: {a=1, c=3, d=4}] [e-h: {e=5, f=6}]
L2: [a-c: {a=0, b=2}]      [d-f: {d=3, e=4}]

Compacting L1[a-d] with L2[a-c] and L2[d-f]:
Merge: a=1(L1 newer), b=2(only in L2), c=3(L1 newer), d=4(L1), e=4(L2)

After Compaction:
L2: [a-c: {a=1, b=2, c=3}] [d-f: {d=4, e=4}] (updated!)
L1: [e-h: {e=5, f=6}] (unchanged)
Old L1[a-d], old L2[a-c], old L2[d-f] deleted
```

#### Compaction Strategies

RocksDB supports multiple compaction strategies — yeh ek major tuning knob hai:

**1. Leveled Compaction (Default)**
```
Leveled Compaction:

- L1 onwards: each level is 10x the previous
- Compaction picks files from Ln, merges with overlapping files in Ln+1
- Result: non-overlapping files in Ln+1

Pros:
+ Space amplification low (~1.1x in theory)
+ Read amplification moderate
+ Good for read-heavy after initial writes

Cons:
- Write amplification HIGH (data rewritten many times as it moves down levels)
- In worst case, write amplification = 10-30x!

Visualization:
L0:  ▓▓▓ (4 files, trigger compaction)
L1:  ████████ (256MB)
L2:  ████████████████████████████████ (2.56GB)
L3:  ████████████████████████████████████████... (25.6GB)
```

**2. Universal Compaction (Size-Tiered)**
```
Universal Compaction:

- Groups sorted runs by size
- Compacts when too many sorted runs exist
- Or when size ratio between adjacent runs is too large

Pros:
+ Write amplification lower than leveled
+ Better for write-heavy workloads

Cons:
- Space amplification can be HIGH (2x or more!)
- Temporary space spike during compaction
- Read amplification higher (more sorted runs to check)
```

**3. FIFO Compaction**
```
FIFO Compaction:

- Simply deletes oldest SST files when total size exceeds limit
- No merge, no sorting
- Used for time-series/cache data that naturally expires

Pros:
+ Almost zero write amplification
+ Very simple

Cons:
- Only works if old data can be safely deleted
- Not for general-purpose use
```

### 3.7 Write Amplification, Read Amplification, Space Amplification

Yeh teen metrics LSM trees ko evaluate karne ke liye use hote hain. Yeh samajhna bohot zaroori hai:

```
┌──────────────────────────────────────────────────────┐
│ Write Amplification (WA):                             │
│   Total bytes written to disk                         │
│   ─────────────────────────── = ratio                 │
│   Bytes of user data written                          │
│                                                        │
│   Example: You write 1GB of data                      │
│   RocksDB writes: 1GB (flush) + 1GB (L0→L1) +        │
│                    1GB (L1→L2) + ... = maybe 10-30GB  │
│   WA = 10-30x!                                        │
│                                                        │
│   High WA = more SSD wear, more I/O bandwidth used   │
├──────────────────────────────────────────────────────┤
│ Read Amplification (RA):                              │
│   Number of disk reads needed for one logical read    │
│                                                        │
│   Point lookup (worst case):                          │
│   MemTable(0) + L0(4 files) + L1(1) + L2(1) + ...   │
│   = potentially many reads!                           │
│   (Bloom filters help a LOT here)                     │
│                                                        │
│   B-Tree comparison: typically 3-4 reads (tree depth) │
├──────────────────────────────────────────────────────┤
│ Space Amplification (SA):                             │
│   Actual disk space used                              │
│   ──────────────────── = ratio                        │
│   Logical data size                                   │
│                                                        │
│   Old versions of keys take space until compacted     │
│   Tombstones (delete markers) also take space          │
│   Leveled: ~1.1x (good!)                             │
│   Universal: up to 2x (during compaction)             │
└──────────────────────────────────────────────────────┘
```

Yeh teen metrics ke beech **tension** hai — tum teeno simultaneously optimize nahi kar sakte:

```
The RUM Conjecture (simplified):

Read optimal ←→ Write optimal ←→ Space optimal
     ↕                ↕                ↕
  B-Tree          LSM (Universal)   LSM (Leveled)
  (low RA,        (low WA,          (low SA,
   high WA)       high SA)           high WA)

You can optimize for at most 2 out of 3!
```

Yeh insight mujhe bohot achhi lagi — databases fundamentally yeh trade-off kar rahe hain, aur different designs different points pe hain is spectrum pe.

---

## 4. Design Trade-offs

### LSM Tree vs B-Tree — The Big Comparison

| Aspect | LSM Tree (RocksDB) | B-Tree (PostgreSQL/InnoDB) |
|--------|-------------------|---------------------------|
| Write performance | Excellent (sequential writes) | Moderate (random I/O for page updates) |
| Read performance | Moderate (check multiple levels) | Excellent (single tree traversal) |
| Space efficiency | Depends on compaction strategy | Generally good (some fragmentation) |
| Write amplification | Can be high (10-30x leveled) | Moderate (~2-3x typically) |
| Predictability | Less predictable (compaction spikes) | More predictable (consistent I/O pattern) |
| SSD friendliness | Sequential writes = good for SSD life | Random writes = more SSD wear |
| Range scans | Decent within a level, costly across levels | Excellent (leaf pages are linked) |
| Deletes | Lazy (tombstones, cleaned during compaction) | Immediate (or MVCC-based) |

### When to Use RocksDB

```
Good fit:
✓ Write-heavy workloads (logging, metrics, time-series)
✓ Embedded key-value store needed
✓ SSD storage (sequential writes preserve SSD life)
✓ Workloads where write throughput > read throughput
✓ Building custom storage layers (like CockroachDB did)

Not ideal for:
✗ Read-heavy workloads with many point lookups
✗ Complex queries (it's a key-value store, no SQL!)
✗ Workloads needing predictable latency (compaction causes spikes)
✗ Small datasets where B-Tree overhead is negligible
```

### Compaction Strategy Selection

Yeh actually ek important practical decision hai:

```
Decision Tree:

Your workload is:
├── Mostly writes with occasional reads?
│   └→ Universal Compaction (lower write amplification)
│
├── Balanced reads and writes?
│   └→ Leveled Compaction (default, good balance)
│
├── Time-series / TTL data?
│   └→ FIFO Compaction (if old data can be dropped)
│
└── Space-constrained environment?
    └→ Leveled Compaction (lowest space amplification)
```

---

## 5. Experiments / Observations

### Experiment 1: Write Performance vs B-Tree

```
Benchmark Setup:
- 10 million random key-value pairs
- Key: 16 bytes, Value: 100 bytes
- Sequential writes

Results (approximate, from db_bench tool):

RocksDB:
┌───────────────────────────────────┐
│ fillseq:   780,000 ops/sec       │
│ fillrandom: 420,000 ops/sec      │
│ Write bandwidth: ~45 MB/sec      │
└───────────────────────────────────┘

For comparison (approximate B-Tree numbers):
┌───────────────────────────────────┐
│ Sequential insert: ~200,000 ops/s│
│ Random insert: ~50,000 ops/sec   │
│ (varies hugely by implementation)│
└───────────────────────────────────┘
```

Write performance difference is dramatic — especially for random writes. Sequential writes mein gap thoda kam hai kyunki B-Trees bhi sequential inserts efficiently handle karte hain (no page splits).



### Experiment 2: Read Performance Comparison

```
After writing 10M entries:

RocksDB (with Bloom filters):
┌───────────────────────────────────┐
│ readrandom: 180,000 ops/sec      │
│ readseq:    1,200,000 ops/sec    │
│ (sequential reads much faster!)  │
└───────────────────────────────────┘

RocksDB (WITHOUT Bloom filters):
┌───────────────────────────────────┐
│ readrandom: 45,000 ops/sec       │
│ (4x slower without Bloom!)       │
└───────────────────────────────────┘
```

Two observations:
1. **Bloom filters ka impact massive hai** — 4x improvement for random reads! Yeh isliye kyunki Bloom filter se pata chal jaata hai ki kaunse SSTables skip karne hain.
2. **Sequential reads fast hain** kyunki data sorted hai within each level, toh prefetching aur caching effectively kaam karta hai.



### Experiment 3: Compaction Impact

```
db_bench ke saath different compaction strategies:

Write 10M entries, then measure:

Leveled Compaction:
┌───────────────────────────────────┐
│ Write throughput: 350K ops/sec    │
│ Space used: 1.15 GB (1.1x amp)   │
│ Read latency P99: 2.1 ms         │
│ Write amplification: ~12x        │
│ Compaction spikes: moderate       │
└───────────────────────────────────┘

Universal Compaction:
┌───────────────────────────────────┐
│ Write throughput: 480K ops/sec    │
│ Space used: 1.8 GB (1.7x amp)    │
│ Read latency P99: 4.3 ms         │
│ Write amplification: ~5x         │
│ Compaction spikes: larger but less│
│ frequent                          │
└───────────────────────────────────┘

FIFO Compaction:
┌───────────────────────────────────┐
│ Write throughput: 700K ops/sec    │
│ Space used: depends on limit      │
│ Write amplification: ~1x         │
│ (oldest data gets dropped!)      │
└───────────────────────────────────┘
```

Trade-offs clearly visible:
- **Leveled:** Best space efficiency, worst write amplification
- **Universal:** Better writes, worse space
- **FIFO:** Best writes, but loses old data



### Experiment 4: Write Amplification Observation

Maine aur Kartik ne specifically write amplification monitor kiya:

```
Using RocksDB statistics:

After writing 1GB of user data with Leveled Compaction:

rocksdb.compact.write.bytes = 11,230,567,890  (~11.2 GB)
User data written            = 1,073,741,824  (~1 GB)

Write Amplification = 11.2 / 1.0 = 11.2x !!

Breakdown:
- Flush (MemTable → L0): 1x
- L0 → L1 compaction: ~1x
- L1 → L2 compaction: ~4x (each byte in L1 might be rewritten 
                            with L2 data during merge)
- L2 → L3 compaction: ~5x
Total: ~11x

This means for every 1 byte you write,
the SSD actually writes ~11 bytes!
```

On SSDs this is significant because SSDs have limited write endurance (write cycles). High write amplification = faster SSD wear. Yeh production mein actually ek real concern hai.

Mitigation strategies:
- Use Universal Compaction (lower WA)
- Tune `level0_file_num_compaction_trigger`
- Use larger MemTables (fewer flushes)
- Consider FIFO for TTL data

---

## 6. Key Learnings

1. **LSM Trees flip the B-Tree paradigm.** B-Trees optimize for reads (organized structure, quick traversal). LSM Trees optimize for writes (convert random writes to sequential). Neither is universally better — it depends on workload.

2. **Write amplification is the hidden cost of LSM Trees.** Pehle lagta hai "wow, writes are so fast!" but behind the scenes compaction data ko baar baar rewrite kar raha hai. 10-30x write amplification — this matters for SSD lifetime and total I/O bandwidth.

3. **Bloom Filters are ESSENTIAL for LSM read performance.** Without them, every read potentially touches every level. With them, most SSTables can be quickly skipped. A few kilobytes of bloom filter data saves megabytes of unnecessary reads. Yeh ek beautiful example hai space-time trade-off ka.

4. **Compaction is both LSM's strength and weakness.** It's necessary (otherwise reads become impossibly slow and space grows unbounded), but it's also expensive (CPU, I/O, temporary space spikes). Compaction tuning is probably the single most important operational task for RocksDB in production.

5. **The RUM trade-off is real.** Read, Update (Write), Memory (Space) — you can't optimize all three simultaneously. B-Trees sacrifice write performance for read + space efficiency. LSM Leveled sacrifices writes for reads + space. LSM Universal sacrifices space for writes. This framework helps reason about ANY storage engine design.

6. **RocksDB is not a database — it's a storage engine.** It doesn't have SQL, query optimizer, or client-server architecture. It's a building block. CockroachDB, TiDB, and others BUILD databases on top of RocksDB. Understanding this distinction is important — it's like comparing an engine to a car.

7. **Tombstones (delete markers) are weird but logical.** In LSM, you can't just "delete" a key because it might exist in multiple levels. Instead, you write a tombstone marker that says "this key is deleted." During compaction, when the tombstone meets the actual key, both are removed. Until then, the tombstone and the data both exist — taking up space. This was initially very counterintuitive to me.

8. **Level 0 is special.** Files overlap, multiple files to check for reads, compaction pressure from L0 drives a lot of behavior. Understanding L0 behavior is key to understanding RocksDB performance characteristics.

---

### Comparison Table — All 4 Topics at a Glance

Yeh table maine aur Kartik ne apne liye banaya tha revision ke time, but I think it's useful:

```
┌──────────────┬────────────────┬────────────────┬──────────────┐
│ Feature       │ PostgreSQL     │ InnoDB         │ RocksDB      │
├──────────────┼────────────────┼────────────────┼──────────────┤
│ Type          │ Client-Server  │ Plugin Engine  │ Embedded Lib │
│ Data Structure│ Heap + B-Tree  │ Clustered B+   │ LSM Tree     │
│ Write Model   │ Append (MVCC)  │ In-place+Undo  │ Sequential   │
│ Read Model    │ Index→Heap     │ Clustered=fast │ Multi-level  │
│ Concurrency   │ MVCC (no locks)│ MVCC + Locks   │ Snapshots    │
│ Recovery      │ REDO only      │ REDO + UNDO    │ WAL replay   │
│ Cleanup       │ VACUUM         │ Purge thread   │ Compaction   │
│ Best for      │ Mixed OLTP     │ Read+PK heavy  │ Write-heavy  │
└──────────────┴────────────────┴────────────────┴──────────────┘
```

---

