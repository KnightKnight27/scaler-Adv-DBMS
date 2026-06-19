**Name:** Jatin Chulet  
**Roll No:** 2024BCS10213

---

# PostgreSQL Internals — Deep Dive

> Yeh wala topic mujhe sabse zyada interesting laga but sabse mushkil bhi. PostgreSQL ke source code mein ghusa toh ek alag hi duniya thi. Trying to explain jo samjha — apne tarike se, with diagrams aur examples.

---

## 1. Problem Background

PostgreSQL basically ek bohot mature, feature-rich relational database hai. But sirf "it stores data" bol dena kaafi nahi — asli interesting baat hai **kaise** yeh sab karta hai internally.

Jab main pehli baar PostgreSQL use kiya tha toh bas `CREATE TABLE`, `INSERT`, `SELECT` — yahi tha mere liye. But jab internals mein ghusa toh realize hua ki ek simple `SELECT * FROM users WHERE id = 5` ke peeche kitna kuch ho raha hai — buffer manager pages la raha hai, B-tree index traverse ho raha hai, MVCC visibility check ho raha hai, query planner decide kar raha hai kaunsa plan use karna hai... bohot kuch!

Yeh document mein 4 major components cover karunga:
1. **Buffer Manager** — memory aur disk ke beech ka bridge
2. **B-Tree Implementation** — indexes kaise kaam karte hain
3. **MVCC** — concurrency kaise handle hoti hai
4. **WAL** — data kaise safe rehta hai crash ke baad

Plus end mein ek `EXPLAIN ANALYZE` experiment bhi hai.

---

## 2. Architecture Overview

Pehle ek high-level picture:

```
┌──────────────────────────────────────────────────────────────────┐
│                     PostgreSQL Backend Process                    │
│                                                                    │
│  ┌─────────┐    ┌──────────────┐    ┌────────────┐               │
│  │ Parser  │───→│ Planner/     │───→│  Executor  │               │
│  │         │    │ Optimizer    │    │            │               │
│  └─────────┘    └──────────────┘    └─────┬──────┘               │
│                                           │                       │
│                                    ┌──────▼──────┐               │
│                                    │  Access     │               │
│                                    │  Methods    │               │
│                                    │ (heap,index)│               │
│                                    └──────┬──────┘               │
│                                           │                       │
│  ┌────────────────────────────────────────▼───────────────────┐  │
│  │                    Buffer Manager                          │  │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐           │  │
│  │  │Page 1│ │Page 2│ │Page 3│ │Page 4│ │ ...  │           │  │
│  │  │      │ │      │ │(dirty)│ │      │ │      │           │  │
│  │  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘           │  │
│  │               Shared Buffer Pool                          │  │
│  └───────────────────────┬───────────────────────────────────┘  │
│                          │                                       │
└──────────────────────────┼───────────────────────────────────────┘
                           │ read/write
                    ┌──────▼──────┐
                    │    Disk     │
                    │ ┌────────┐  │
                    │ │Data    │  │
                    │ │Files   │  │
                    │ ├────────┤  │
                    │ │WAL     │  │
                    │ │Files   │  │
                    │ └────────┘  │
                    └─────────────┘
```

Query ka flow basically aisa hai:
1. SQL string aati hai → **Parser** usse parse tree banata hai
2. **Planner/Optimizer** best execution plan decide karta hai (yeh statistics use karta hai — `pg_statistic` se)
3. **Executor** plan ko execute karta hai
4. Executor ko data chahiye → **Access Methods** se maangta hai (heap scan, index scan, etc.)
5. Access methods **Buffer Manager** se pages maangte hain
6. Buffer manager check karta hai — page memory mein hai toh direct de deta hai, nahi toh disk se read karta hai

---

## 3. Internal Design

### 3.1 Buffer Manager

**Location in source:** `src/backend/storage/buffer/`

Yeh PostgreSQL ka ek critical component hai. Think of it as a **cache** between disk and the rest of the system.

#### Shared Buffers — The Buffer Pool

PostgreSQL ek fixed-size **shared buffer pool** maintain karta hai in shared memory. Default size 128MB hoti hai (but production mein usually 25% of RAM set karte hain).

```
Shared Buffer Pool:
┌────────────────────────────────────────────────────────────┐
│                                                             │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐          │
│  │ Buffer │  │ Buffer │  │ Buffer │  │ Buffer │   ...     │
│  │ Desc 0 │  │ Desc 1 │  │ Desc 2 │  │ Desc 3 │          │
│  │        │  │        │  │        │  │        │          │
│  │ tag:   │  │ tag:   │  │ tag:   │  │ tag:   │          │
│  │(rel,   │  │(rel,   │  │(rel,   │  │(rel,   │          │
│  │ fork,  │  │ fork,  │  │ fork,  │  │ fork,  │          │
│  │ block) │  │ block) │  │ block) │  │ block) │          │
│  │        │  │        │  │        │  │        │          │
│  │flags:  │  │flags:  │  │flags:  │  │flags:  │          │
│  │dirty?  │  │dirty?  │  │dirty?  │  │dirty?  │          │
│  │valid?  │  │valid?  │  │valid?  │  │valid?  │          │
│  │pinned? │  │pinned? │  │pinned? │  │pinned? │          │
│  │refcount│  │refcount│  │refcount│  │refcount│          │
│  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘          │
│      │           │           │           │                │
│  ┌───▼────┐  ┌───▼────┐  ┌───▼────┐  ┌───▼────┐         │
│  │ 8KB    │  │ 8KB    │  │ 8KB    │  │ 8KB    │          │
│  │ Page   │  │ Page   │  │ Page   │  │ Page   │   ...    │
│  │ Data   │  │ Data   │  │ Data   │  │ Data   │          │
│  └────────┘  └────────┘  └────────┘  └────────┘          │
│                                                            │
│  Hash Table: (relation_id, fork, block_num) → buffer_id   │
│  ┌─────────────────────────────────────────────────┐      │
│  │ hash(tag) → bucket → buffer descriptor          │      │
│  └─────────────────────────────────────────────────┘      │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

Har buffer slot mein:
- **Buffer Descriptor** — metadata (kaunsa page hai, dirty hai ya nahi, kitne processes use kar rahe hain)
- **Buffer Page** — actual 8KB data

Jab koi page chahiye, process kuch aisa karta hai:

```
ReadBuffer(relation, block_number):
    1. tag = (relation_id, fork_number, block_number)
    2. Check hash table: kya yeh tag already buffer pool mein hai?
       ├── YES → pin that buffer, return it (CACHE HIT!)
       └── NO  → CACHE MISS:
                  a. Find a free/evictable buffer
                  b. If chosen buffer is dirty → write it to disk first
                  c. Read the requested page from disk into this buffer
                  d. Update hash table with new tag
                  e. Pin and return
```

#### Buffer Replacement — Clock Sweep Algorithm

Jab buffer pool full ho jaaye aur naya page laana ho, toh kaunsa purana page hataana hai? PostgreSQL **Clock Sweep** algorithm use karta hai (yeh LRU ka ek efficient variant hai).

```
Clock Sweep Algorithm:

Imagine buffer slots arranged in a circle:

         ┌──[0]──[1]──[2]──┐
         │                   │
        [7]     CLOCK       [3]
         │      HAND→       │
         └──[6]──[5]──[4]──┘

Har buffer slot ka ek "usage_count" hota hai.

Jab ek buffer access hota hai → usage_count++ (max 5)
Jab naya buffer chahiye:
  1. Clock hand current position se start
  2. Check current slot:
     - usage_count > 0? → Decrement it, move hand forward
     - usage_count == 0 AND not pinned? → EVICT this one! Use it.
     - pinned? → Skip, move forward
  3. Repeat until found
```

Yeh actually bohot clever hai. Frequently accessed pages ka usage_count high rehta hai toh woh survive karte hain multiple sweeps. Rarely accessed pages quickly evict ho jaate hain. Ek tarah se "second chance" algorithm hai — har page ko ek aur chance milta hai before eviction.

Mujhe pehle LRU zyada intuitive lagta tha, but clock sweep ka advantage hai ki yeh **O(1) for access** hai (just increment counter), jabki pure LRU mein list manipulation karni padti hai har access pe.

#### Background Writer (bgwriter)

Yeh ek background process hai jo periodically dirty buffers ko disk pe likhta hai. Kyun? Taki jab actually kisi buffer ko evict karna ho, toh woh already clean ho aur quickly reuse ho sake. Agar bgwriter nahi hota toh eviction ke time synchronously disk write karna padta — slow!

```
bgwriter cycle:
  while true:
    sleep(bgwriter_delay)  // default 200ms
    scan buffer pool
    find dirty, unpinned buffers with low usage_count
    write them to disk (up to bgwriter_lru_maxpages per cycle)
    mark them as clean
```

---

### 3.2 B-Tree Implementation (nbtree)

**Location in source:** `src/backend/access/nbtree/`

PostgreSQL ka B-Tree implementation Lehman & Yao's B-Tree paper pe based hai — yeh concurrent access ke liye optimized hai (multiple processes simultaneously ek hi index ko read aur modify kar sakte hain without holding long locks).

#### Index Page Layout

```
B-Tree Index Page Structure:

┌──────────────────────────────────────────┐
│           Page Header (24 bytes)          │
├──────────────────────────────────────────┤
│        BTPageOpaqueData (Special)         │
│  - btpo_prev (left sibling page)         │
│  - btpo_next (right sibling page)        │
│  - btpo_level (0=leaf, >0=internal)      │
│  - btpo_flags (leaf? root? deleted?)     │
├──────────────────────────────────────────┤
│      Line Pointers (Item IDs)            │
│  [ItemId 1] [ItemId 2] [ItemId 3] ...   │
├──────────────────────────────────────────┤
│           Free Space                      │
├──────────────────────────────────────────┤
│        Index Tuples (bottom-up)           │
│  ┌─────────────────────────┐             │
│  │ IndexTuple:             │             │
│  │  - key value(s)         │             │
│  │  - TID (table row loc)  │  ← leaf    │
│  │  OR                     │             │
│  │  - child page number    │  ← internal│
│  └─────────────────────────┘             │
└──────────────────────────────────────────┘
```

#### Search Path — Index Scan kaise hota hai

Maan lo tumne query likhi: `SELECT * FROM users WHERE email = 'kartik@example.com'` aur `email` column pe B-Tree index hai.

```
Search Process:

Step 1: Start at root page
┌─────────────────────────────┐
│ Root (level 2)               │
│ [aaa@... | jjj@... | sss@..]│
│ [ptr0|ptr1|ptr2|ptr3]       │
└─────────┬───────────────────┘
          │ "kartik@..." >= "jjj@..." 
          │ and < "sss@..."
          │ so follow ptr2
          ▼
Step 2: Internal page
┌─────────────────────────────┐
│ Internal (level 1)           │
│ [jjj@... | mmm@... | ppp@..]│
│ [ptr0|ptr1|ptr2|ptr3]       │
└─────────┬───────────────────┘
          │ follow appropriate pointer
          ▼
Step 3: Leaf page
┌─────────────────────────────────────┐
│ Leaf (level 0)                       │
│ [jack@..→TID(5,3)]                  │
│ [kartik@example.com→TID(12,7)] ← FOUND!│
│ [megha@..→TID(8,1)]                 │
│                                      │
│ prev←[page 44]  next→[page 46]     │
└─────────────────────────────────────┘

Step 4: Use TID(12,7) to fetch actual row
         from heap page 12, item 7
```

TID ka matlab hai "Tuple ID" — basically (page_number, item_number) pair jo exact location batata hai heap mein.

#### Insert aur Page Split

Jab nayi key insert karni ho:

```
Insert "kate@..." into B-Tree:

1. Search for correct leaf page (same search as above)
2. Found leaf page with space? → Insert there, done!
3. Leaf page FULL? → PAGE SPLIT!

Page Split Process:
BEFORE:
┌──────────────────────────────┐
│ Leaf Page (FULL)              │
│ [aaa] [bbb] [ccc] [ddd]     │
│ [eee] [fff] [ggg] [hhh]     │
│ ← no space for [kate]! →    │
└──────────────────────────────┘

AFTER:
┌───────────────┐    ┌───────────────┐
│ Old Leaf       │    │ New Leaf       │
│ [aaa] [bbb]   │───→│ [eee] [fff]   │
│ [ccc] [ddd]   │    │ [ggg] [hhh]   │
│               │    │ [kate] ← NEW! │
└───────────────┘    └───────────────┘
                          │
         Parent gets new key ──→ [eee] added to parent
         pointing to new leaf

Agar parent bhi full ho → parent bhi split hota hai!
(Cascading splits — rare but possible)
```

Interesting fact: Lehman & Yao algorithm mein har page ka ek "high key" hota hai — yeh batata hai ki is page pe maximum kya value ho sakti hai. Yeh concurrent operations ke liye help karta hai — agar koi process split ke beech mein aa jaaye toh bhi correctly navigate kar sake.

---

### 3.3 MVCC (Multi-Version Concurrency Control)

Yeh PostgreSQL ka dil hai. Isko samajhna = PostgreSQL ko samajhna.

#### Heap Tuple Versioning

Har row (tuple) ke header mein do crucial fields hain:

```
┌──────────────────────────────────────────────┐
│ Tuple Header Fields for MVCC:                │
│                                               │
│ t_xmin = Transaction ID that INSERTED this   │
│          tuple (yeh row kisne banai?)         │
│                                               │
│ t_xmax = Transaction ID that DELETED/UPDATED  │
│          this tuple (kisne delete/update ki?) │
│          0 means "not deleted yet"            │
│                                               │
│ t_cid  = Command ID within the transaction   │
│          (transaction ke andar kaunsa command)│
│                                               │
│ t_ctid = Current Tuple ID — points to latest │
│          version if this tuple was updated    │
└──────────────────────────────────────────────┘
```

#### Example — Step by Step

Chalo ek practical example dekhte hain:

```
Initial State: Empty table "accounts"

Transaction 100 (T100): INSERT INTO accounts VALUES (1, 'Kartik', 5000);

Heap Page:
┌─────────────────────────────────────────┐
│ Tuple A:                                 │
│   xmin=100, xmax=0, data=(1,'Kartik',5000)│
│   t_ctid = (page0, item1) ← points to  │
│             itself (latest version)      │
└─────────────────────────────────────────┘
Status: T100 commits. Tuple A is LIVE.
```

Ab update karte hain:

```
Transaction 200 (T200): UPDATE accounts SET balance=3000 WHERE id=1;

What PostgreSQL does internally:
1. Find Tuple A
2. Set Tuple A's xmax = 200  (mark as "deleted by T200")
3. Insert NEW Tuple B with the updated data
4. Set Tuple A's t_ctid to point to Tuple B

Heap Page (after update):
┌─────────────────────────────────────────┐
│ Tuple A (OLD — "dead" after T200 commits):│
│   xmin=100, xmax=200                    │
│   data=(1, 'Kartik', 5000)               │
│   t_ctid → Tuple B                      │
├─────────────────────────────────────────┤
│ Tuple B (NEW — current version):         │
│   xmin=200, xmax=0                      │
│   data=(1, 'Kartik', 3000)               │
│   t_ctid → itself                       │
└─────────────────────────────────────────┘
```

Dekha? **Dono versions** exist kar rahe hain simultaneously! Old version delete nahi hua, bas xmax set ho gaya.

#### Visibility Rules

Ab sabse tricky part — kaunsi version kis transaction ko visible hai?

```
Visibility Check (simplified):

For a tuple to be VISIBLE to transaction T:

1. xmin must be committed AND xmin < T's snapshot
   (the tuple was inserted by a committed txn that 
    started before my snapshot)

2. Either:
   a. xmax is 0 (not deleted)
   OR
   b. xmax is NOT committed yet (deleting txn hasn't committed)
   OR  
   c. xmax committed but AFTER T's snapshot
      (deleted after I took my snapshot — I should still see it)
```

Isko ek scenario se samajhte hain:

```
Timeline:
────────────────────────────────────────────►
T100: INSERT (1,'Kartik',5000) → COMMIT
          T200: BEGIN
          T200: reads accounts → sees (1,'Kartik',5000) ✓
                    T300: BEGIN
                    T300: UPDATE balance=3000 → COMMIT
          T200: reads accounts AGAIN
          T200: STILL sees (1,'Kartik',5000)! ← old version
          T200: because T200's snapshot was taken before T300

This is SNAPSHOT ISOLATION!
```

Yeh mujhe bohot mind-blowing laga pehli baar — T200 ko purana data dikh raha hai even though T300 ne update karke commit bhi kar diya. Because PostgreSQL ne T200 ko ek "snapshot" diya jab T200 shuru hua, aur uske baad ka koi committed change T200 ko visible nahi hai. Consistent view of the database!

#### VACUUM — The Cleanup Crew

Ab problem yeh hai ki ye dead tuples accumulate hote jaate hain. 10 baar update karo, 10 versions pad jaayengi ek row ki. Isliye VACUUM chahiye:

```
VACUUM Process:

BEFORE VACUUM:
┌─────────────────────────────────┐
│ Tuple A: xmin=100, xmax=200    │ ← DEAD (no txn needs this)
│ Tuple B: xmin=200, xmax=300    │ ← DEAD
│ Tuple C: xmin=300, xmax=0      │ ← LIVE (current version)
│ Tuple D: xmin=150, xmax=250    │ ← DEAD
│                                  │
│ Free Space: very little!        │
└─────────────────────────────────┘

VACUUM checks: kya koi running transaction hai jo 
in dead tuples ko access kar sakta hai?

Agar nahi → mark them as free space!

AFTER VACUUM:
┌─────────────────────────────────┐
│ [FREE SPACE — reusable]        │
│ [FREE SPACE — reusable]        │
│ Tuple C: xmin=300, xmax=0      │ ← LIVE
│ [FREE SPACE — reusable]        │
│                                  │
│ Free Space: recovered!          │
└─────────────────────────────────┘
```

Important: VACUUM by default **does not return space to OS** — it just marks dead tuple space as reusable within the table. `VACUUM FULL` actually rewrites the table aur space OS ko return karta hai, but yeh exclusive lock leta hai — production pe avoid karo!

**Autovacuum** — PostgreSQL mein ek background daemon hai jo automatically VACUUM chalaata hai jab dead tuples ek threshold cross kar jaayein. Iske parameters tune kar sakte ho:
- `autovacuum_vacuum_threshold` = minimum dead tuples before vacuum
- `autovacuum_vacuum_scale_factor` = fraction of table size

---

### 3.4 WAL (Write-Ahead Logging)

#### Concept

WAL ka ek simple rule hai: **Pehle log likho, phir data change karo.**

```
Write-Ahead Logging Rule:
┌────────────────────────────────────────────────────┐
│                                                     │
│  Before modifying ANY data page,                    │
│  you MUST first write a WAL record                  │
│  describing the change.                             │
│                                                     │
│  And that WAL record must be flushed                │
│  to disk before the transaction can                 │
│  be considered committed.                           │
│                                                     │
└────────────────────────────────────────────────────┘
```

Kyun? Agar system crash ho jaaye data write karte time:
- Data files corrupt/incomplete ho sakti hain
- BUT WAL file safely likhi hui hai disk pe
- Recovery ke time WAL replay karke data files fix kar sakte hain

#### WAL Record Structure

```
WAL Record:
┌──────────────────────────────────┐
│ Header:                           │
│  - xl_tot_len (record length)    │
│  - xl_xid (transaction ID)       │
│  - xl_prev (previous record LSN) │
│  - xl_info (operation type)      │
│  - xl_rmid (resource manager)    │
│  - xl_crc (checksum)            │
├──────────────────────────────────┤
│ Data:                             │
│  - Block references              │
│  - Actual change data             │
│  (enough info to REDO the change)│
└──────────────────────────────────┘
```

#### LSN — Log Sequence Number

Har WAL record ka ek unique identifier hota hai called **LSN (Log Sequence Number)**. Yeh basically byte offset hai WAL stream mein.

```
WAL Stream:
┌────────┬────────┬────────┬────────┬────────┐
│Record 1│Record 2│Record 3│Record 4│Record 5│
│LSN:0/10│LSN:0/80│LSN:0/F0│LSN:1/20│LSN:1/90│
└────────┴────────┴────────┴────────┴────────┘

Har data page mein bhi ek LSN stored hota hai (pd_lsn in page header)
— yeh batata hai ki is page pe last WAL record kaunsa apply hua

Recovery ke time:
if page_lsn < WAL_record_lsn:
    → yeh change apply nahi hua, REDO it!
if page_lsn >= WAL_record_lsn:
    → yeh change already applied hai, SKIP!
```

#### Checkpointing

Agar sab changes sirf WAL mein hain aur data files mein nahi, toh crash ke baad saara WAL replay karna padega — bohot slow! Isliye **checkpoints** hote hain:

```
Checkpoint Process:
1. Mark the current WAL position as checkpoint start
2. Flush ALL dirty buffers to data files on disk
3. Write a checkpoint record to WAL
4. Now we know: everything before this checkpoint 
   is safely in data files
5. Old WAL files before checkpoint can be recycled

Timeline:
─────────────────────────────────────────────────►
  [WAL records] [WAL records] [CHECKPOINT] [WAL records]
                               ▲
                               │
                    If crash happens here,
                    only need to replay WAL 
                    from last checkpoint onwards
                    (not from the beginning!)
```

`checkpoint_timeout` (default 5 min) aur `max_wal_size` (default 1GB) — inhe tune karte hain production mein. Frequent checkpoints = faster recovery but more I/O. Infrequent checkpoints = less I/O but longer recovery.

#### Crash Recovery

```
Crash Recovery Process:

1. PostgreSQL starts up after crash
2. Finds the last checkpoint in WAL
3. Reads WAL records from that point forward
4. For each WAL record:
   a. Read the target data page
   b. Check page's LSN vs WAL record's LSN
   c. If page is older → apply (REDO) the change
   d. If page already has this change → skip
5. After all WAL records are replayed
   → Database is consistent!
   → Start accepting connections

This is called "REDO-only recovery" — 
PostgreSQL doesn't need UNDO because 
uncommitted transactions' changes are 
simply not visible (MVCC handles this!)
```

Yeh actually ek bohot elegant design hai — MVCC aur WAL saath milke kaam karte hain. WAL se REDO hota hai, MVCC se UNDO ki zaroorat nahi. Compare this with InnoDB jisme explicitly undo logs chahiye — iske baare mein MySQL_InnoDB topic mein likhunga.

---

## 4. Design Trade-offs

### Buffer Manager Trade-offs

| Decision | Advantage | Disadvantage |
|----------|-----------|--------------|
| Fixed-size shared buffer pool | Predictable memory usage, shared across connections | Tumhe manually size tune karna padta hai (`shared_buffers`). Too small = poor cache hit ratio, Too large = OS page cache ke liye kam memory |
| Clock sweep (vs LRU) | O(1) access, simple implementation, no per-access list manipulation | Not as precise as true LRU, sometimes evicts pages that LRU would keep |
| 8KB page size | Good balance for mixed workloads | Could be suboptimal for purely sequential scans (larger pages better) or point lookups on narrow tables (smaller pages better) |

### MVCC Trade-offs

Yeh sabse important trade-off hai PostgreSQL mein:

```
Append-only MVCC (PostgreSQL approach):
┌─────────────────────────────────────┐
│ PROS:                                │
│ ✓ Readers never block writers        │
│ ✓ Writers never block readers        │
│ ✓ No undo logs needed               │
│ ✓ Simple crash recovery (REDO only) │
│ ✓ Old versions available for snapshot│
│                                      │
│ CONS:                                │
│ ✗ Dead tuples accumulate (bloat)    │
│ ✗ VACUUM overhead                    │
│ ✗ Table size can grow unnecessarily │
│ ✗ Index entries for dead tuples     │
│ ✗ Transaction ID wraparound risk    │
│ ✗ UPDATE = DELETE + INSERT          │
│   (slower than in-place update)     │
└─────────────────────────────────────┘
```

### WAL Trade-offs

- **Synchronous commit** (default) — har commit pe WAL disk pe flush hota hai. Safe but slow for lots of small transactions.
- `synchronous_commit = off` — WAL flush lazily hota hai. Faster but risk of losing last few transactions on crash. Kuch applications ke liye acceptable hai.
- **Full page writes** — first modification after checkpoint pe poora page WAL mein likha jaata hai (partial write protection ke liye). Safety badhti hai but WAL size bhi.

---

## 5. Experiments / Observations

### EXPLAIN ANALYZE — Multi-table Join Query

Maine ek sample database banaya with 3 tables:

```sql
-- Setup
CREATE TABLE departments (
    dept_id SERIAL PRIMARY KEY,
    dept_name VARCHAR(50)
);

CREATE TABLE employees (
    emp_id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    dept_id INTEGER REFERENCES departments(dept_id),
    salary INTEGER
);

CREATE TABLE projects (
    project_id SERIAL PRIMARY KEY,
    project_name VARCHAR(100),
    lead_emp_id INTEGER REFERENCES employees(emp_id),
    budget INTEGER
);

-- Inserted ~50,000 employees, 20 departments, 500 projects
-- Sample employees: Jatin in dept 5, Kartik in dept 3, etc.
-- (used generate_series + random data)
```

Query:
```sql
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT) 
SELECT d.dept_name, 
       COUNT(e.emp_id) as emp_count, 
       AVG(e.salary) as avg_salary,
       COUNT(p.project_id) as project_count
FROM departments d
JOIN employees e ON e.dept_id = d.dept_id
LEFT JOIN projects p ON p.lead_emp_id = e.emp_id
WHERE e.salary > 50000
GROUP BY d.dept_name
ORDER BY avg_salary DESC;
```

Output (approximately):
```
Sort  (cost=3245.67..3245.72 rows=20 width=78) 
      (actual time=87.234..87.241 rows=20 loops=1)
  Sort Key: (avg(e.salary)) DESC
  Sort Method: quicksort  Memory: 26kB
  Buffers: shared hit=1823 read=342
  ->  HashAggregate  (cost=3244.89..3245.14 rows=20 width=78)
      (actual time=87.198..87.215 rows=20 loops=1)
        Group Key: d.dept_name
        Batches: 1  Memory Usage: 24kB
        ->  Hash Left Join  (cost=1234.56..3100.23 rows=28932 width=26)
            (actual time=12.456..72.891 rows=24856 loops=1)
              Hash Cond: (e.emp_id = p.lead_emp_id)
              ->  Hash Join  (cost=120.50..1789.34 rows=25123 width=22)
                  (actual time=1.234..35.678 rows=24856 loops=1)
                    Hash Cond: (e.dept_id = d.dept_id)
                    ->  Seq Scan on employees e  (cost=0.00..1400.00 rows=25123 width=16)
                        (actual time=0.012..18.456 rows=24856 loops=1)
                          Filter: (salary > 50000)
                          Rows Removed by Filter: 25144
                          Buffers: shared hit=1200 read=180
                    ->  Hash  (cost=120.00..120.00 rows=20 width=14)
                        (actual time=0.089..0.091 rows=20 loops=1)
                          Buckets: 1024  Batches: 1  Memory Usage: 9kB
                          ->  Seq Scan on departments d  ...
              ->  Hash  (cost=789.00..789.00 rows=500 width=8)
                  (actual time=3.456..3.458 rows=500 loops=1)
                    ->  Seq Scan on projects p  ...
Planning Time: 0.456 ms
Execution Time: 87.345 ms
```

![Terminal Analysis Output Screenshot](PostgreSQL_Internals\explain_analyze_output.png)


#### Meri Analysis:

**1. Join Strategy:** PostgreSQL ne Hash Joins choose kiye — makes sense because:
- Departments table chhoti hai (20 rows) — hash table memory mein easily fit hota hai
- Projects table bhi relatively small (500 rows)
- Hash Join O(N+M) hai jabki Nested Loop O(N*M) hoga — for large tables hash better hai

**2. Sequential Scan on employees:** Even though `salary` pe filter hai, PostgreSQL ne Seq Scan kiya instead of Index Scan. Kyun? 
- Roughly 50% rows qualify (salary > 50000 — random data thi 0-100000)
- Jab itne saare rows qualify karte hain, Seq Scan actually faster hota hai Index Scan se (sequential I/O vs random I/O)
- Planner ne yeh decision `pg_statistic` ke data se liya — column ka histogram dekha aur estimate kiya ki ~50% rows qualify karenge

**3. Buffer Statistics:** `shared hit=1823 read=342`
- 1823 pages buffer cache mein the (hit = memory se read hua)
- 342 pages disk se read karne pade (miss)
- Hit ratio = 1823/(1823+342) ≈ 84%
- Not bad, but production mein typically 99%+ hit ratio chahiye

**4. Cost Estimates vs Actuals:**
- Estimated rows = 25123, Actual = 24856 — pretty close! Statistics achhe hain
- Jab statistics outdated ho jaayein (ANALYZE nahi chala) toh yeh estimates bohot off ho sakte hain → bad query plans

**Agar main `CREATE INDEX idx_salary ON employees(salary)` banaata:**
- For this query (50% selectivity) → probably still Seq Scan because too many rows match
- But for `WHERE salary > 90000` (maybe 10% selectivity) → Index Scan use hoga

#### pg_statistic Connection

PostgreSQL `ANALYZE` command chalane pe statistics collect karta hai:

```sql
-- Yeh statistics manually refresh karta hai
ANALYZE employees;

-- pg_statistic mein yeh store hota hai (approximate):
-- - Most common values aur unki frequencies
-- - Histogram boundaries (data distribution)
-- - Number of distinct values
-- - Null fraction  
-- - Average width
-- - Correlation (physical vs logical ordering)

-- Human-readable view:
SELECT attname, null_frac, avg_width, n_distinct,
       most_common_vals, most_common_freqs, histogram_bounds
FROM pg_stats 
WHERE tablename = 'employees' AND attname = 'salary';
```

Planner in statistics ko dekhke decide karta hai:
- Kaunsa join algorithm use karna hai (Nested Loop, Hash Join, Merge Join)
- Index Scan ya Seq Scan
- Join order (kaunsi table pehle access karo)
- Expected row counts at each stage

Agar statistics stale hain → planner galat decisions leta hai → slow queries!

Isliye `autovacuum` process periodically `ANALYZE` bhi chalata hai tables pe.

---

## 6. Key Learnings

1. **Buffer Manager is the unsung hero.** Sab kuch iske through jaata hai. Production performance tuning ka sabse pehla step `shared_buffers` sahi set karna hai. Aur buffer hit ratio monitor karna — agar 95% se neeche hai toh problem hai.

2. **MVCC is beautiful but messy.** The concept is elegant — old versions rakh lo, snapshot se decide karo kya dikhana hai. But practically dead tuples ka build-up, VACUUM ki zaroorat, table bloat — yeh sab iska "price" hai. Understanding MVCC properly takes time, mujhe 3-4 baar padhna pada before it clicked.

3. **WAL + MVCC together = genius.** PostgreSQL ko UNDO logs nahi chahiye (unlike InnoDB) because MVCC ke through old versions available hain. WAL sirf REDO karta hai. This simplifies crash recovery significantly.

4. **Query planner is only as good as its statistics.** Yeh realize hone mein thoda time laga. Agar `ANALYZE` nahi chala, toh planner blindly estimate karega aur probably galat plan choose karega. Autovacuum by default enabled hai which helps, but heavy write workloads mein sometimes manual `ANALYZE` bhi zaroori hota hai.

5. **B-Tree page splits are expensive.** Isliye sequential inserts (auto-increment IDs) better hain performance-wise — new tuples hamesha rightmost leaf mein jaate hain, minimal splits. Random inserts (like UUIDs) cause more splits aur zyada fragmentation.

6. **Clock Sweep > Simple LRU** for databases — yeh mujhe nahi pata tha pehle. The simplicity and O(1) access cost make it practical for high-concurrency scenarios where every microsecond matters.

7. **PostgreSQL ka source code** actually bohot well-organized aur well-commented hai. `src/backend/storage/buffer/README` file mein buffer manager ka detailed explanation hai — PostgreSQL devs ne khud likha hai. If you want to go deeper, woh README files in source code gold hain.

---


