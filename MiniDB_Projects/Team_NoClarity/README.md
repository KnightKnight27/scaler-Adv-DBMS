# MiniDB Relational Database Engine

**Team Name:** Team_NoClarity  
**Team Members:**  
- **Name:** Rachit S  
- **Scaler Roll Number:** 24bcs10139  
- **Email:** vangsur68@gmail.com  

---

## 1. Project Overview & Extension Track
MiniDB is a lightweight relational database system written in modern C++ (C++17) designed for educational exploration of database internals.
- **Goals:** Modularity, structural correctness, and zero memory leaks.
- **Extension Track:** **Track B — Concurrency (MVCC)** (Targeting high concurrent read throughput using snapshot visibility version chains, replacing traditional strict lock blockages).

---

## 2. System Architecture (Milestone 1)

At the current milestone, the architecture consists of the Disk Storage, Cache Replacer, and Page Layout modules:

```
     +--------------------------+
     |     Client / main.cpp    |
     +--------------------------+
                  │
                  ▼
     +--------------------------+
     |    BufferPoolManager     | <───────> [ ClockReplacer ]
     +--------------------------+           (Victims tracking)
                  │
                  ▼
     +--------------------------+
     |       DiskManager        |
     +--------------------------+
                  │
                  ▼
      [ db_file.db (4KB Pages) ]
```

---

## 3. Storage Layer Details

### Page Format
We employ a **Slotted Page Layout** to store variable-length records on fixed 4KB pages:
- **Header:**
  - Bytes `0-1`: `slot_count` (uint16_t)
  - Bytes `2-3`: `free_space_pointer` (uint16_t, initialized to 4096)
- **Slot Array:**
  - Placed at the top (starts at byte 4) growing downwards. Each slot represents `Slot { offset, length }`.
- **Tuple Storage:**
  - Inserted from the end of the page (byte 4095) growing upwards towards the slot array.
- **Compaction:**
  - Deletions write a tombstone marker (`0xFFFF`) in the slot array. When page space is exhausted, active tuples are packed tightly towards the end of the page while maintaining original slot array indices.

### Heap Files & Buffer Pool
- **Heap Files:** DiskManager manages the sequential allocation of pages in the backing binary file.
- **Buffer Pool Manager:** Holds an array of cached page frames in memory. Page frames are checked out with a `pin_count` increments. Candidates for eviction must have a `pin_count == 0` and are selected via the clock eviction replacer.

---

## 4. How to Run (Milestone 1)

### Dependencies
- Modern C++ Compiler (GCC 7+, MSVC 2019+, or Clang 6+) supporting C++17.
- POSIX threads support (`-pthread`).

### Build and Run Test Harness
Navigate to the root workspace directory and compile the executable:
```bash
g++ -std=c++17 -pthread -Isrc \
  MiniDB_Projects/Team_NoClarity/src/storage/disk_manager.cpp \
  MiniDB_Projects/Team_NoClarity/src/storage/slotted_page.cpp \
  MiniDB_Projects/Team_NoClarity/src/storage/clock_replacer.cpp \
  MiniDB_Projects/Team_NoClarity/src/storage/buffer_pool_manager.cpp \
  MiniDB_Projects/Team_NoClarity/main.cpp \
  -o minidb_m1_test

# Run the test binary
.\minidb_m1_test.exe
```
This test runner executes the slotted page formatting, clock eviction cycles, and cache write-back assertions.
