# MiniDB - Advanced relational database engine

A premium relational database system built from core components to demonstrate advanced DBMS concepts, with a spectacular state-of-the-art visual web dashboard environment.

## Team ConcurrencyMasters
- **Sameer Khan** (sam13sameer.13khan@gmail.com)
- **Rishi Harti** (rishie.harti@gmail.com)
- **Srujan Gowda** (kssrujangowda37@gmail.com)
- **Pranay Reddy** (pranay.24bcs10133@sst.scaler.com)

---

## Technical Specifications

### 1. Storage Engine
- **Page-based Heap Files**: Organizes physical records within 4KB pages containing a rich binary slot directory header keeping track of offsets, entry sizes, and active delete status.
- **Buffer Pool Manager**: Implements frames supporting thread pinning, unpinning, dirty flags, and robust eviction policies (LRU/Clock eviction checks) to avoid page memory leaks.

### 2. B+ Tree Indexing
- Maps Primary Key numbers directly to `RID` containing specific `pageId` and `slotId` locators.
- Completely supports insertion node splits and deletion collapses maintaining index height balance.

### 3. Volcano Execution & Cost-Based Optimizer
- **Volcano Iterators**: volcano iterator architecture featuring standard relational interface methods (`init`, `next`, `close`) for query operations `SeqScan`, `IndexScan`, `Filter`, `NestedLoopJoin`, and `Projection`.
- **Cost-Based Plan Selection**: Analyzes selectivity factors, table sizes, and node counts to selectively prefer `IndexScan` over `SeqScan`.

### 4. MVCC Concurrency (Track B — Chosen Extension)
- Swapped standard Two-Phase Locking (2PL) with high-throughput **Multi-Version Concurrency Control (MVCC)**.
- Each record stores transaction metadata (`xmin`, `xmax`).
- Snapshot Visibility rules determine whether concurrently executing sessions can read deleted/modified records.

### 5. WAL & Crash Recovery
- Implements Write-Ahead Logging.
- Recovery re-enacts the core Analysis phase, Redo phase (repeating history), and Undo phase (reversing active/aborted uncommitted logs) maintaining durability invariants.

---

## Installation & How to Run

1. Install dependencies:
   ```bash
   npm install
   ```
2. Run local dev server:
   ```bash
   npm run dev
   ```
