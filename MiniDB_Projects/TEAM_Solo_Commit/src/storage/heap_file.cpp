#include "heap_file.h"

namespace minidb {

RID HeapFile::Insert(const std::string& rec) {
    // Try the last page first; allocate a fresh one only if it does not fit.
    if (!page_ids_.empty()) {
        int pid = page_ids_.back();
        Page* p = bp_->FetchPage(pid);
        if (p) {
            int slot = p->Insert(rec);
            if (slot >= 0) {
                bp_->Unpin(pid, true);
                return RID(pid, slot);
            }
            bp_->Unpin(pid, false);
        }
    }
    int new_id = INVALID_PAGE_ID;
    Page* np = bp_->NewPage(&new_id);
    page_ids_.push_back(new_id);
    int slot = np->Insert(rec);
    bp_->Unpin(new_id, true);
    return RID(new_id, slot);
}

bool HeapFile::Get(RID rid, std::string* out) const {
    Page* p = bp_->FetchPage(rid.page_id);
    if (!p) return false;
    bool ok = p->Read(rid.slot_num, out);
    bp_->Unpin(rid.page_id, false);
    return ok;
}

bool HeapFile::Delete(RID rid) {
    Page* p = bp_->FetchPage(rid.page_id);
    if (!p) return false;
    bool ok = p->Delete(rid.slot_num);
    bp_->Unpin(rid.page_id, ok);
    return ok;
}

void HeapFile::Iterator::SkipToLive() {
    while (page_pos_ < hf_->page_ids_.size()) {
        int pid = hf_->page_ids_[page_pos_];
        Page* p = hf_->bp_->FetchPage(pid);
        int n = p ? p->NumSlots() : 0;
        while (slot_ < n) {
            bool live = p->IsLive(slot_);
            if (live) { hf_->bp_->Unpin(pid, false); return; }
            ++slot_;
        }
        if (p) hf_->bp_->Unpin(pid, false);
        ++page_pos_;
        slot_ = 0;
    }
    // Reached end: normalize to the end() sentinel (page_pos_ == size, slot_ == 0).
    slot_ = 0;
}

}  // namespace minidb
