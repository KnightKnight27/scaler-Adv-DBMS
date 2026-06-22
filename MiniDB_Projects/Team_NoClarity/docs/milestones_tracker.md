# MiniDB Project Milestone Tracker & Roadmap

**Team Name:** Team_NoClarity  
**Team Members:**  
- Rachit S (Roll Number: 24bcs10139, Email: vangsur68@gmail.com)

---

## 1. Project Overview & Extension Track
MiniDB is a transactional relational database built from foundational components.
- **Chosen Extension Track:** **Track B — Concurrency (MVCC)** (Implementing Multi-Version Concurrency Control visibility rules and transaction isolation on top of storage/locking).

---

## 2. Milestone Log

### 🏁 Milestone 1: Foundational Storage & Cache (Completed)
- **Objective:** Implement a thread-safe disk-block manager, slotted-page record storage formatting, and a Clock/Second-Chance buffer pool cache.
- **Key Achievements:**
  - Designed the `DiskManager` mapping logical 4KB pages to physical offsets.
  - Implemented the `SlottedPage` parsing raw bytes in-place to support insertion, deletion, and index-stable compaction.
  - Coded the `ClockReplacer` using a scanning clock hand to identify unpinned eviction victims.
  - Structured the `BufferPoolManager` cache layer coordinating disk file reads/writes with memory page allocations.

---

## 3. MiniDB Architecture Evolution

As the system evolves through each milestone, more components will be integrated into the architecture.

### Milestone 1 Component Architecture
```mermaid
graph TD
    subgraph Client Application
        App[main.cpp]
    end

    subgraph Buffer Cache Layer (RAM)
        BPM[BufferPoolManager]
        CR[ClockReplacer]
        Frames[Pool of Page Frames]
    end

    subgraph Storage Layout
        SP[SlottedPage Wrapper]
    end

    subgraph Disk I/O Layer (Filesystem)
        DM[DiskManager]
        DBFile[(minidb.db File)]
    end

    App -->|Requests Page / New Page| BPM
    BPM -->|Identifies Evictions| CR
    BPM -->|Loads / Flushes Frames| Frames
    App -->|Reads / Writes Records| SP
    SP -->|Interprets In-Place Memory| Frames
    BPM -->|Sync block reads/writes| DM
    DM -->|Reads & Writes| DBFile
```

---

## 4. Design Trade-offs & Decisions (Milestone 1)

1. **In-Place Slotted-Page Memory Allocation:**
   - Instead of heap allocating structured C++ structs and serializing them, `SlottedPage` casts the raw `char[PAGE_SIZE]` directly to slot arrays. This provides native speed matching production systems.
2. **Stable Record IDs (RID):**
   - RIDs use logical offsets `(page_id, slot_index)`. When tuples are deleted, the space becomes fragmented. Calling `CompactPage()` defragments the storage by packing tuples while keeping their slot array indexes stable. This ensures indices referencing these records are not broken during defragmentation.
3. **Double-Buffering & Eviction Safety:**
   - Eviction of page frames is strictly bounded by pinning. Pinned pages are completely hidden from the `ClockReplacer` candidates list, ensuring in-use frames are never overwritten.
