#include "storage.h"
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace minidb {

// ── DiskManager ───────────────────────────────────────────────────────────────

DiskManager::DiskManager(const std::string& path) {
    // Try opening an existing file first; if it doesn't exist, create it.
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_.is_open()) {
        file_.open(path, std::ios::binary | std::ios::in | std::ios::out
                       | std::ios::trunc);
    }
    if (!file_.is_open())
        throw std::runtime_error("Cannot open database file: " + path);

    // Count existing pages by dividing file size by PAGE_SIZE.
    file_.seekg(0, std::ios::end);
    num_pages_ = static_cast<int>(file_.tellg() / PAGE_SIZE);
}

DiskManager::~DiskManager() { file_.close(); }

void DiskManager::read_page(int page_id, Page& out) {
    assert(page_id >= 0 && page_id < num_pages_);
    file_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    file_.read(out.data(), PAGE_SIZE);
}

void DiskManager::write_page(int page_id, const Page& p) {
    assert(page_id >= 0 && page_id < num_pages_);
    file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    file_.write(p.data(), PAGE_SIZE);
    file_.flush();
}

int DiskManager::new_page() {
    // Extend the file by one zeroed page and return the new page_id.
    Page blank{};
    file_.seekp(static_cast<std::streamoff>(num_pages_) * PAGE_SIZE);
    file_.write(blank.data(), PAGE_SIZE);
    file_.flush();
    return num_pages_++;
}

// ── BufferPool ────────────────────────────────────────────────────────────────

BufferPool::BufferPool(DiskManager& disk, int capacity)
    : disk_(disk), frames_(capacity) {}

BufferPool::~BufferPool() { flush_all(); }

Page* BufferPool::fetch(int page_id) {
    // Cache hit: page is already in a frame.
    if (auto it = page_to_frame_.find(page_id); it != page_to_frame_.end()) {
        int fi = it->second;
        frames_[fi].pins++;
        // Promote to front of LRU (most recently used).
        lru_.erase(lru_pos_[fi]);
        lru_.push_front(fi);
        lru_pos_[fi] = lru_.begin();
        hits++;
        return &frames_[fi].page;
    }

    // Cache miss: need to bring the page in from disk.
    misses++;
    int fi = find_victim();
    if (fi < 0)
        throw std::runtime_error("Buffer pool exhausted: all frames are pinned");
    load(fi, page_id);
    frames_[fi].pins = 1;
    return &frames_[fi].page;
}

void BufferPool::unpin(int page_id, bool dirty) {
    auto it = page_to_frame_.find(page_id);
    if (it == page_to_frame_.end()) return;
    int fi = it->second;
    if (frames_[fi].pins > 0) frames_[fi].pins--;
    if (dirty) frames_[fi].dirty = true;
}

void BufferPool::flush_all() {
    for (auto& f : frames_) {
        if (f.page_id >= 0 && f.dirty) {
            disk_.write_page(f.page_id, f.page);
            f.dirty = false;
        }
    }
}

int BufferPool::find_victim() {
    // First check for completely empty frames (pool is not yet full).
    for (int i = 0; i < static_cast<int>(frames_.size()); i++) {
        if (frames_[i].page_id == -1) return i;
    }
    // Walk from LRU end toward MRU end looking for an unpinned frame.
    for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
        int fi = *it;
        if (frames_[fi].pins == 0) {
            // Write back if modified.
            if (frames_[fi].dirty) {
                disk_.write_page(frames_[fi].page_id, frames_[fi].page);
                frames_[fi].dirty = false;
            }
            page_to_frame_.erase(frames_[fi].page_id);
            lru_pos_.erase(fi);
            lru_.erase(std::next(it).base());
            frames_[fi].page_id = -1;
            return fi;
        }
    }
    return -1; // all frames pinned
}

void BufferPool::load(int fi, int page_id) {
    // Bring the page from disk.  new_page() always writes a blank page first,
    // so every valid page_id is guaranteed to be on disk.
    disk_.read_page(page_id, frames_[fi].page);
    frames_[fi].page_id = page_id;
    frames_[fi].dirty   = false;
    page_to_frame_[page_id] = fi;
    lru_.push_front(fi);
    lru_pos_[fi] = lru_.begin();
}

// ── Heap page helpers (typed accessors into raw bytes) ────────────────────────

int  Heap::slot_count(const Page& p) { int n; memcpy(&n, p.data(),     4); return n; }
int  Heap::free_top  (const Page& p) { int n; memcpy(&n, p.data() + 4, 4); return n; }
void Heap::set_slot_count(Page& p, int n) { memcpy(p.data(),     &n, 4); }
void Heap::set_free_top  (Page& p, int v) { memcpy(p.data() + 4, &v, 4); }

void Heap::read_slot(const Page& p, int slot, int& off, int& len) {
    const char* base = p.data() + 8 + slot * 8;
    memcpy(&off, base,     4);
    memcpy(&len, base + 4, 4);
}

void Heap::write_slot(Page& p, int slot, int off, int len) {
    char* base = p.data() + 8 + slot * 8;
    memcpy(base,     &off, 4);
    memcpy(base + 4, &len, 4);
}

// ── Heap ──────────────────────────────────────────────────────────────────────

Heap::Heap(BufferPool& pool, DiskManager& disk)
    : pool_(pool), disk_(disk) {}

int Heap::find_or_alloc(int needed) {
    // Slot directory overhead: 8 bytes per slot + 8 bytes for header.
    // The slot directory lives at the front, records at the back.
    // A new record of `needed` bytes fits iff:
    //   free_top - needed  >=  8 + (slot_count + 1) * 8
    for (int pid : page_ids_) {
        Page* p  = pool_.fetch(pid);
        int sc   = slot_count(*p);
        int ft   = free_top(*p);
        int need = 8 + (sc + 1) * 8;  // minimum space for directory
        bool ok  = (ft - needed) >= need;
        pool_.unpin(pid, false);
        if (ok) return pid;
    }
    // All pages are full — allocate a new one and initialize its header.
    int pid = disk_.new_page();
    page_ids_.push_back(pid);
    Page* p = pool_.fetch(pid);
    set_slot_count(*p, 0);
    set_free_top(*p, PAGE_SIZE);   // records grow downward from the end
    pool_.unpin(pid, true);
    return pid;
}

RID Heap::insert(const std::string& row) {
    int len = static_cast<int>(row.size());
    int pid = find_or_alloc(len);

    Page* p  = pool_.fetch(pid);
    int sc   = slot_count(*p);
    int ft   = free_top(*p);

    // Place the record just below the current free-top pointer.
    int rec_start = ft - len;
    memcpy(p->data() + rec_start, row.data(), len);

    // Add a new slot directory entry pointing to this record.
    write_slot(*p, sc, rec_start, len);
    set_slot_count(*p, sc + 1);
    set_free_top(*p, rec_start);

    pool_.unpin(pid, true);
    return {pid, sc};   // RID = (page_id, slot index)
}

bool Heap::fetch(RID rid, std::string& out) {
    Page* p = pool_.fetch(rid.page_id);
    int off, len;
    read_slot(*p, rid.slot, off, len);
    bool alive = (len != -1);
    if (alive) {
        // Copy data into `out` BEFORE unpinning — the page may be evicted after.
        out.assign(p->data() + off, len);
    }
    pool_.unpin(rid.page_id, false);
    return alive;
}

void Heap::remove(RID rid) {
    Page* p = pool_.fetch(rid.page_id);
    int off, len;
    read_slot(*p, rid.slot, off, len);
    // Mark the slot as a tombstone by setting length = -1.
    // Space is not reclaimed — acceptable for our course scope.
    write_slot(*p, rid.slot, off, -1);
    pool_.unpin(rid.page_id, true);
}

std::vector<std::pair<RID, std::string>> Heap::scan() {
    std::vector<std::pair<RID, std::string>> rows;
    for (int pid : page_ids_) {
        Page* p = pool_.fetch(pid);
        int sc  = slot_count(*p);
        for (int s = 0; s < sc; s++) {
            int off, len;
            read_slot(*p, s, off, len);
            if (len == -1) continue;   // skip tombstones
            rows.push_back({{pid, s}, std::string(p->data() + off, len)});
        }
        pool_.unpin(pid, false);
    }
    return rows;
}

} // namespace minidb
