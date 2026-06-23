#include "storage/buffer_pool.h"
#include <stdexcept>
#include <cstring>

namespace minidb {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

BufferPool::BufferPool(size_t pool_size, DiskManager* dm)
    : pool_size_(pool_size), dm_(dm),
      frames_(pool_size), pin_counts_(pool_size, 0)
{
    // Initially every frame is free – populate LRU list with all frame IDs.
    for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size_); ++i) {
        lru_.push_back(i);
        lru_iter_[i] = std::prev(lru_.end());
    }
}

BufferPool::~BufferPool() { FlushAll(); }

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

// Evict whatever is currently in `fid` (write if dirty, remove from page_table).
// Caller must hold latch_.
static void Evict(frame_id_t fid, Page* page, DiskManager* dm,
                  std::unordered_map<page_id_t, frame_id_t>& pt)
{
    page_id_t old = page->GetPageId();
    if (old == INVALID_PAGE_ID) return;
    if (page->IsDirty()) {
        dm->WritePage(old, page->GetData());
        page->SetDirty(false);
    }
    pt.erase(old);
    page->SetPageId(INVALID_PAGE_ID);
}

bool BufferPool::FindVictim(frame_id_t* out_fid) {
    // Scan LRU from back (least recently used) toward front.
    for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
        frame_id_t fid = *it;
        if (pin_counts_[fid] == 0) { *out_fid = fid; return true; }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

Page* BufferPool::NewPage(page_id_t* out_page_id) {
    std::lock_guard<std::mutex> lk(latch_);
    frame_id_t fid;
    if (!FindVictim(&fid)) return nullptr;

    Evict(fid, &frames_[fid], dm_, page_table_);

    page_id_t pid = dm_->AllocatePage();
    *out_page_id  = pid;
    frames_[fid].SetPageId(pid);
    frames_[fid].ResetMemory();
    frames_[fid].SetDirty(false);
    pin_counts_[fid] = 1;
    page_table_[pid] = fid;

    // Move to MRU.
    lru_.erase(lru_iter_[fid]);
    lru_.push_front(fid);
    lru_iter_[fid] = lru_.begin();
    return &frames_[fid];
}

Page* BufferPool::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t fid = it->second;
        pin_counts_[fid]++;
        lru_.erase(lru_iter_[fid]);
        lru_.push_front(fid);
        lru_iter_[fid] = lru_.begin();
        return &frames_[fid];
    }
    frame_id_t fid;
    if (!FindVictim(&fid)) return nullptr;
    Evict(fid, &frames_[fid], dm_, page_table_);
    frames_[fid].SetPageId(page_id);
    dm_->ReadPage(page_id, frames_[fid].GetData());
    frames_[fid].SetDirty(false);
    pin_counts_[fid] = 1;
    page_table_[page_id] = fid;
    lru_.erase(lru_iter_[fid]);
    lru_.push_front(fid);
    lru_iter_[fid] = lru_.begin();
    return &frames_[fid];
}

bool BufferPool::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    frame_id_t fid = it->second;
    if (pin_counts_[fid] <= 0) return false;
    if (is_dirty) frames_[fid].SetDirty(true);
    pin_counts_[fid]--;
    return true;
}

bool BufferPool::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    frame_id_t fid = it->second;
    dm_->WritePage(page_id, frames_[fid].GetData());
    frames_[fid].SetDirty(false);
    return true;
}

void BufferPool::FlushAll() {
    std::lock_guard<std::mutex> lk(latch_);
    for (auto& [pid, fid] : page_table_) {
        if (frames_[fid].IsDirty()) {
            dm_->WritePage(pid, frames_[fid].GetData());
            frames_[fid].SetDirty(false);
        }
    }
}

bool BufferPool::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return true;
    frame_id_t fid = it->second;
    if (pin_counts_[fid] > 0) return false;
    page_table_.erase(it);
    frames_[fid].ResetMemory();
    frames_[fid].SetPageId(INVALID_PAGE_ID);
    frames_[fid].SetDirty(false);
    pin_counts_[fid] = 0;
    lru_.erase(lru_iter_[fid]);
    lru_.push_back(fid);
    lru_iter_[fid] = std::prev(lru_.end());
    return true;
}

} // namespace minidb
