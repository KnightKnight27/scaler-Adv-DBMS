#include "storage/heap_file.h"
#include <iostream>
#include <algorithm>
#include <functional>

// ─── Record serialization ─────────────────────────────────────────────────────
//
// Binary layout: [4 bytes key][null-terminated value string]
// Total size = 4 + value.size() + 1 (null byte)

size_t Record::serialize(char* buf, size_t buf_size) const {
    size_t total = sizeof(int32_t) + value.size() + 1;
    if (total > buf_size) {
        return 0;  // won't fit
    }
    std::memcpy(buf, &key, sizeof(int32_t));
    std::memcpy(buf + sizeof(int32_t), value.c_str(), value.size() + 1);
    return total;
}

Record Record::deserialize(const char* buf, size_t len) {
    Record r;
    if (len < sizeof(int32_t)) {
        return r;
    }
    std::memcpy(&r.key, buf, sizeof(int32_t));
    // The rest is the null-terminated value string
    if (len > sizeof(int32_t)) {
        r.value = std::string(buf + sizeof(int32_t));
    }
    return r;
}

// ─── HeapFile constructor ─────────────────────────────────────────────────────

HeapFile::HeapFile(const std::string& table_name, BufferPool& bp)
    : table_name_(table_name), bp_(bp)
{}

// ─── create ───────────────────────────────────────────────────────────────────

void HeapFile::create() {
    PageID pid;
    Page* p = bp_.newPage(pid);
    if (!p) {
        std::cerr << "[HeapFile] ERROR: could not allocate first page for table '"
                  << table_name_ << "'\n";
        return;
    }
    first_page_id_ = pid;
    last_page_id_  = pid;
    bp_.unpinPage(pid, false);  // page was just initialized, not dirty yet
}

// ─── open ─────────────────────────────────────────────────────────────────────

void HeapFile::open(PageID first_page_id) {
    first_page_id_ = first_page_id;
    // Scan forward from first page to find the last page
    // (pages form a simple list: we use their page_id + 1 as the next page,
    //  but for simplicity in this implementation the heap is contiguous)
    last_page_id_ = first_page_id;
}

// ─── insertRecord ─────────────────────────────────────────────────────────────

RecordID HeapFile::insertRecord(const Record& rec) {
    // Serialize the record to a temporary buffer
    char buf[MAX_RECORD_SIZE + sizeof(int32_t) + 1];
    size_t data_len = rec.serialize(buf, sizeof(buf));
    if (data_len == 0) {
        std::cerr << "[HeapFile] ERROR: record too large to serialize\n";
        return RecordID{};
    }

    // Find a page with enough space
    // Needed = data_len + one slot entry
    size_t space_needed = data_len + sizeof(Slot);
    PageID pid = findOrAllocatePage(space_needed);
    if (pid == INVALID_PAGE_ID) {
        std::cerr << "[HeapFile] ERROR: cannot find or allocate page\n";
        return RecordID{};
    }

    // Pin the page and write the record
    Page* page = bp_.pinPage(pid);
    if (!page) {
        return RecordID{};
    }

    SlotID sid = writeToPage(page, buf, static_cast<uint16_t>(data_len));
    bp_.unpinPage(pid, true);  // dirty = true because we wrote to it

    if (sid == INVALID_SLOT_ID) {
        return RecordID{};
    }

    record_count_++;
    return RecordID{pid, sid};
}

// ─── getRecord ────────────────────────────────────────────────────────────────

std::optional<Record> HeapFile::getRecord(const RecordID& rid) {
    if (!rid.isValid()) {
        return std::nullopt;
    }

    Page* page = bp_.pinPage(rid.page_id);
    if (!page) {
        return std::nullopt;
    }

    if (rid.slot_id >= page->header.num_slots) {
        bp_.unpinPage(rid.page_id, false);
        return std::nullopt;
    }

    const Slot& s = page->slot(rid.slot_id);
    if (s.length == 0) {
        // Deleted slot
        bp_.unpinPage(rid.page_id, false);
        return std::nullopt;
    }

    Record r = Record::deserialize(page->recordData(rid.slot_id), s.length);
    bp_.unpinPage(rid.page_id, false);
    return r;
}

// ─── deleteRecord ─────────────────────────────────────────────────────────────

bool HeapFile::deleteRecord(const RecordID& rid) {
    if (!rid.isValid()) {
        return false;
    }

    Page* page = bp_.pinPage(rid.page_id);
    if (!page) {
        return false;
    }

    if (rid.slot_id >= page->header.num_slots) {
        bp_.unpinPage(rid.page_id, false);
        return false;
    }

    Slot& s = page->slot(rid.slot_id);
    if (s.length == 0) {
        bp_.unpinPage(rid.page_id, false);
        return false;  // already deleted
    }

    // Mark slot as deleted by zeroing length (offset stays for compaction later)
    s.length = 0;
    if (record_count_ > 0) {
        record_count_--;
    }

    bp_.unpinPage(rid.page_id, true);  // dirty
    return true;
}

// ─── scanAll ──────────────────────────────────────────────────────────────────
//
// Iterates through all pages in the heap from first_page_id_ to last_page_id_
// and visits every non-deleted slot.

void HeapFile::scanAll(std::function<bool(const RecordID&, const Record&)> callback) {
    if (first_page_id_ == INVALID_PAGE_ID) {
        return;
    }

    // Pages are contiguous from first_page_id_ to last_page_id_
    for (PageID pid = first_page_id_; pid <= last_page_id_; ++pid) {
        Page* page = bp_.pinPage(pid);
        if (!page) {
            continue;
        }

        uint16_t num_slots = page->header.num_slots;

        for (SlotID sid = 0; sid < num_slots; ++sid) {
            const Slot& s = page->slot(sid);
            if (s.length == 0) {
                continue;  // deleted
            }

            Record r = Record::deserialize(page->recordData(sid), s.length);
            RecordID rid{pid, sid};

            bp_.unpinPage(pid, false);  // unpin before callback to avoid pin leaks
            bool should_continue = callback(rid, r);
            page = bp_.pinPage(pid);    // re-pin to continue iterating this page
            if (!page || !should_continue) {
                if (page) bp_.unpinPage(pid, false);
                return;
            }
        }

        bp_.unpinPage(pid, false);
    }
}

// ─── findOrAllocatePage ───────────────────────────────────────────────────────
//
// Scans pages from last_page_id_ backwards looking for free space.
// If none found, allocates a new page.

PageID HeapFile::findOrAllocatePage(size_t space_needed) {
    // Check the last page first (common case — most recent page still has space)
    if (last_page_id_ != INVALID_PAGE_ID) {
        Page* page = bp_.pinPage(last_page_id_);
        if (page && page->freeSpace() >= space_needed) {
            bp_.unpinPage(last_page_id_, false);
            return last_page_id_;
        }
        if (page) {
            bp_.unpinPage(last_page_id_, false);
        }
    }

    // Allocate a fresh page
    PageID new_pid;
    Page* new_page = bp_.newPage(new_pid);
    if (!new_page) {
        return INVALID_PAGE_ID;
    }

    bp_.unpinPage(new_pid, false);
    last_page_id_ = new_pid;
    return new_pid;
}

// ─── writeToPage ──────────────────────────────────────────────────────────────
//
// Writes data into a new slot at the back of the page.
// Slotted page layout: slot array grows from front, data grows from back.

SlotID HeapFile::writeToPage(Page* page, const char* data, uint16_t data_len) {
    // Calculate how much space we need: the data itself + one more slot entry
    size_t needed = data_len + sizeof(Slot);
    if (page->freeSpace() < needed) {
        return INVALID_SLOT_ID;
    }

    // New data starts at free_space_end - data_len (growing backward)
    uint16_t new_free_end = page->header.free_space_end - data_len;
    std::memcpy(page->body + new_free_end, data, data_len);

    // Add a new slot at the end of the slot array
    SlotID sid = page->header.num_slots;
    page->slot(sid) = Slot{new_free_end, data_len};

    // Update the header
    page->header.num_slots++;
    page->header.free_space_end = new_free_end;

    return sid;
}
