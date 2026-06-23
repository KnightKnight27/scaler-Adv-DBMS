#include "storage/heap_file.h"
#include <iostream>
#include <algorithm>
#include <functional>

size_t Record::serialize(char* buf, size_t buf_size) const {
    const size_t total_size = sizeof(int32_t) + value.size() + 1;
    if (total_size > buf_size) {
        return 0;
    }
    std::memcpy(buf, &key, sizeof(int32_t));
    std::memcpy(buf + sizeof(int32_t), value.c_str(), value.size() + 1);
    return total_size;
}

Record Record::deserialize(const char* buf, size_t len) {
    Record rec;
    if (len < sizeof(int32_t)) {
        return rec;
    }
    std::memcpy(&rec.key, buf, sizeof(int32_t));
    if (len > sizeof(int32_t)) {
        rec.value = std::string(buf + sizeof(int32_t));
    }
    return rec;
}

HeapFile::HeapFile(const std::string& table_name, BufferPool& bp)
    : table_name_(table_name), bp_(bp) {}

void HeapFile::create() {
    PageID pid;
    Page* page = bp_.newPage(pid);
    if (!page) {
        std::cerr << "[HeapFile] ERROR: Could not allocate first page for table '" << table_name_ << "'\n";
        return;
    }
    first_page_id_ = pid;
    last_page_id_ = pid;
    bp_.unpinPage(pid, false);
}

void HeapFile::open(PageID first_page_id) {
    first_page_id_ = first_page_id;
    last_page_id_ = first_page_id;
}

RecordID HeapFile::insertRecord(const Record& rec) {
    char buf[MAX_RECORD_SIZE + sizeof(int32_t) + 1];
    const size_t data_len = rec.serialize(buf, sizeof(buf));
    if (data_len == 0) {
        std::cerr << "[HeapFile] ERROR: Record too large to serialize\n";
        return RecordID{};
    }

    const size_t space_needed = data_len + sizeof(Slot);
    const PageID pid = findOrAllocatePage(space_needed);
    if (pid == INVALID_PAGE_ID) {
        std::cerr << "[HeapFile] ERROR: Cannot find or allocate page\n";
        return RecordID{};
    }

    Page* page = bp_.pinPage(pid);
    if (!page) {
        return RecordID{};
    }

    const SlotID sid = writeToPage(page, buf, static_cast<uint16_t>(data_len));
    bp_.unpinPage(pid, true);

    if (sid == INVALID_SLOT_ID) {
        return RecordID{};
    }

    record_count_++;
    return RecordID{pid, sid};
}

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

    const Slot& target_slot = page->slot(rid.slot_id);
    if (target_slot.length == 0) {
        bp_.unpinPage(rid.page_id, false);
        return std::nullopt;
    }

    Record rec = Record::deserialize(page->recordData(rid.slot_id), target_slot.length);
    bp_.unpinPage(rid.page_id, false);
    return rec;
}

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

    Slot& target_slot = page->slot(rid.slot_id);
    if (target_slot.length == 0) {
        bp_.unpinPage(rid.page_id, false);
        return false;
    }

    // Set length to 0 to mark slot as deleted (retaining offset)
    target_slot.length = 0;
    if (record_count_ > 0) {
        record_count_--;
    }

    bp_.unpinPage(rid.page_id, true);
    return true;
}

void HeapFile::scanAll(std::function<bool(const RecordID&, const Record&)> callback) {
    if (first_page_id_ == INVALID_PAGE_ID) {
        return;
    }

    for (PageID pid = first_page_id_; pid <= last_page_id_; ++pid) {
        Page* page = bp_.pinPage(pid);
        if (!page) {
            continue;
        }

        const uint16_t slots_to_check = page->header.num_slots;
        for (SlotID sid = 0; sid < slots_to_check; ++sid) {
            const Slot& current_slot = page->slot(sid);
            if (current_slot.length == 0) {
                continue;
            }

            Record rec = Record::deserialize(page->recordData(sid), current_slot.length);
            RecordID rid{pid, sid};

            // Unpin to avoid lock/pin leaks while execution proceeds inside outer scope callbacks
            bp_.unpinPage(pid, false);
            const bool should_continue = callback(rid, rec);
            page = bp_.pinPage(pid);

            if (!page || !should_continue) {
                if (page) {
                    bp_.unpinPage(pid, false);
                }
                return;
            }
        }

        bp_.unpinPage(pid, false);
    }
}

PageID HeapFile::findOrAllocatePage(size_t space_needed) {
    if (last_page_id_ != INVALID_PAGE_ID) {
        Page* page = bp_.pinPage(last_page_id_);
        if (page) {
            const bool has_space = page->freeSpace() >= space_needed;
            bp_.unpinPage(last_page_id_, false);
            if (has_space) {
                return last_page_id_;
            }
        }
    }

    PageID new_pid;
    Page* new_page = bp_.newPage(new_pid);
    if (!new_page) {
        return INVALID_PAGE_ID;
    }

    bp_.unpinPage(new_pid, false);
    last_page_id_ = new_pid;
    return new_pid;
}

SlotID HeapFile::writeToPage(Page* page, const char* data, uint16_t data_len) {
    const size_t required_space = data_len + sizeof(Slot);
    if (page->freeSpace() < required_space) {
        return INVALID_SLOT_ID;
    }

    const uint16_t new_free_end = page->header.free_space_end - data_len;
    std::memcpy(page->body + new_free_end, data, data_len);

    const SlotID sid = page->header.num_slots;
    page->slot(sid) = Slot{new_free_end, data_len};

    page->header.num_slots++;
    page->header.free_space_end = new_free_end;

    return sid;
}
