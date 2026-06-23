#include "storage/heap_file.h"
#include <cstring>
#include <stdexcept>

namespace minidb {

static constexpr int32_t HEADER_SZ  = 8;
static constexpr int32_t MAX_RECS   =
    static_cast<int32_t>((PAGE_SIZE - HEADER_SZ) / sizeof(Record)); // 340

// ─── Header helpers ──────────────────────────────────────────────────────────
page_id_t HeapFile::NextPage  (const char* d) { page_id_t v; std::memcpy(&v, d,   4); return v; }
int32_t   HeapFile::NumRecords(const char* d) { int32_t   v; std::memcpy(&v, d+4, 4); return v; }
void HeapFile::SetNextPage  (char* d, page_id_t v) { std::memcpy(d,   &v, 4); }
void HeapFile::SetNumRecords(char* d, int32_t   v) { std::memcpy(d+4, &v, 4); }
Record*       HeapFile::Slot (char* d, int32_t i)
    { return reinterpret_cast<Record*>(d + HEADER_SZ + i * sizeof(Record)); }
const Record* HeapFile::SlotC(const char* d, int32_t i)
    { return reinterpret_cast<const Record*>(d + HEADER_SZ + i * sizeof(Record)); }

// ─── Constructor ─────────────────────────────────────────────────────────────
HeapFile::HeapFile(BufferPool* bp, page_id_t first_page_id)
    : bp_(bp), first_pid_(first_page_id)
{
    if (first_pid_ == INVALID_PAGE_ID) {
        Page* p = bp_->NewPage(&first_pid_);
        if (!p) throw std::runtime_error("HeapFile: buffer pool full on init");
        char* d = p->GetData();
        page_id_t no_next = INVALID_PAGE_ID; int32_t zero = 0;
        SetNextPage(d, no_next); SetNumRecords(d, zero);
        bp_->UnpinPage(first_pid_, true);
    }
}

// ─── InsertRecord ─────────────────────────────────────────────────────────────
page_id_t HeapFile::InsertRecord(const Record& rec) {
    page_id_t cur = first_pid_;
    while (cur != INVALID_PAGE_ID) {
        Page* p = bp_->FetchPage(cur);
        if (!p) return INVALID_PAGE_ID;
        char* d   = p->GetData();
        int32_t   n    = NumRecords(d);
        page_id_t next = NextPage(d);

        if (n < MAX_RECS) {
            *Slot(d, n) = rec;
            SetNumRecords(d, n + 1);
            bp_->UnpinPage(cur, true);
            return cur;
        }
        bp_->UnpinPage(cur, false);

        if (next == INVALID_PAGE_ID) {
            page_id_t new_pid;
            Page* np = bp_->NewPage(&new_pid);
            if (!np) return INVALID_PAGE_ID;
            char* nd = np->GetData();
            SetNextPage(nd, INVALID_PAGE_ID);
            SetNumRecords(nd, 1);
            *Slot(nd, 0) = rec;
            bp_->UnpinPage(new_pid, true);
            // Link the old last page to new page.
            Page* lp = bp_->FetchPage(cur);
            SetNextPage(lp->GetData(), new_pid);
            bp_->UnpinPage(cur, true);
            return new_pid;
        }
        cur = next;
    }
    return INVALID_PAGE_ID;
}

// ─── ScanAll ─────────────────────────────────────────────────────────────────
std::vector<Record> HeapFile::ScanAll() const {
    std::vector<Record> out;
    page_id_t cur = first_pid_;
    while (cur != INVALID_PAGE_ID) {
        Page* p = bp_->FetchPage(cur);
        if (!p) break;
        const char* d = p->GetData();
        int32_t   n    = NumRecords(d);
        page_id_t next = NextPage(d);
        for (int32_t i = 0; i < n; ++i) {
            const Record* r = SlotC(d, i);
            if (!r->deleted) out.push_back(*r);
        }
        bp_->UnpinPage(cur, false);
        cur = next;
    }
    return out;
}

// ─── DeleteRecord ─────────────────────────────────────────────────────────────
bool HeapFile::DeleteRecord(int32_t id) {
    page_id_t cur = first_pid_;
    while (cur != INVALID_PAGE_ID) {
        Page* p = bp_->FetchPage(cur);
        if (!p) return false;
        char* d   = p->GetData();
        int32_t   n    = NumRecords(d);
        page_id_t next = NextPage(d);
        for (int32_t i = 0; i < n; ++i) {
            Record* r = Slot(d, i);
            if (!r->deleted && r->id == id) {
                r->deleted = 1;
                bp_->UnpinPage(cur, true);
                return true;
            }
        }
        bp_->UnpinPage(cur, false);
        cur = next;
    }
    return false;
}

// ─── SelectRecord ─────────────────────────────────────────────────────────────
bool HeapFile::SelectRecord(int32_t id, Record* out) const {
    page_id_t cur = first_pid_;
    while (cur != INVALID_PAGE_ID) {
        Page* p = bp_->FetchPage(cur);
        if (!p) break;
        const char* d = p->GetData();
        int32_t   n    = NumRecords(d);
        page_id_t next = NextPage(d);
        for (int32_t i = 0; i < n; ++i) {
            const Record* r = SlotC(d, i);
            if (!r->deleted && r->id == id) {
                if (out) *out = *r;
                bp_->UnpinPage(cur, false);
                return true;
            }
        }
        bp_->UnpinPage(cur, false);
        cur = next;
    }
    return false;
}

} // namespace minidb
