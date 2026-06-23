// storage/page.cpp — Implementation of the slotted-page layout.
//
// See page.h for the full binary layout diagram.

#include "storage/page.h"
#include <algorithm>
#include <stdexcept>

namespace minidb {

// ─── Construction ───────────────────────────────────────────
Page::Page() {
    std::memset(data_, 0, PAGE_SIZE);
    set_page_id(INVALID_PAGE_ID);
    set_free_space_offset(static_cast<uint16_t>(PAGE_SIZE));
    set_next_page_id(INVALID_PAGE_ID);
    // slot_count is 0 because memset zeroed everything.
}

// ─── Header accessors ──────────────────────────────────────
page_id_t Page::get_page_id() const {
    page_id_t pid;
    std::memcpy(&pid, data_, sizeof(pid));
    return pid;
}
void Page::set_page_id(page_id_t pid) {
    std::memcpy(data_, &pid, sizeof(pid));
}

uint16_t Page::get_slot_count() const {
    uint16_t sc;
    std::memcpy(&sc, data_ + 4, sizeof(sc));
    return sc;
}
void Page::set_slot_count(uint16_t count) {
    std::memcpy(data_ + 4, &count, sizeof(count));
}

uint16_t Page::get_free_space_offset() const {
    uint16_t fso;
    std::memcpy(&fso, data_ + 6, sizeof(fso));
    return fso;
}
void Page::set_free_space_offset(uint16_t offset) {
    std::memcpy(data_ + 6, &offset, sizeof(offset));
}

page_id_t Page::get_next_page_id() const {
    page_id_t npid;
    std::memcpy(&npid, data_ + 8, sizeof(npid));
    return npid;
}
void Page::set_next_page_id(page_id_t pid) {
    std::memcpy(data_ + 8, &pid, sizeof(pid));
}

// ─── Slot directory helpers ─────────────────────────────────
void Page::get_slot(slot_id_t sid, uint16_t* offset, uint16_t* length) const {
    uint16_t base = PAGE_HEADER_SIZE + sid * SLOT_ENTRY_SIZE;
    std::memcpy(offset, data_ + base,     sizeof(uint16_t));
    std::memcpy(length, data_ + base + 2, sizeof(uint16_t));
}
void Page::set_slot(slot_id_t sid, uint16_t offset, uint16_t length) {
    uint16_t base = PAGE_HEADER_SIZE + sid * SLOT_ENTRY_SIZE;
    std::memcpy(data_ + base,     &offset, sizeof(uint16_t));
    std::memcpy(data_ + base + 2, &length, sizeof(uint16_t));
}

// ─── Raw data access ───────────────────────────────────────
char*       Page::get_data()       { return data_; }
const char* Page::get_data() const { return data_; }

// ─── Free space calculation ─────────────────────────────────
// The area between the end of the slot directory and free_space_offset
// is free.  We must also account for one *new* slot entry if we want to
// insert another tuple.
uint16_t Page::get_free_space() const {
    uint16_t slot_dir_end = PAGE_HEADER_SIZE
                          + (get_slot_count() + 1) * SLOT_ENTRY_SIZE;
    uint16_t fso = get_free_space_offset();
    if (fso <= slot_dir_end) return 0;
    return fso - slot_dir_end;
}

// ─── insert_tuple ───────────────────────────────────────────
slot_id_t Page::insert_tuple(const char* data, uint16_t length) {
    if (length == 0) return INVALID_SLOT_ID;

    uint16_t slot_count = get_slot_count();
    // Space check: after adding one more slot entry, the new
    // free_space_offset must not collide with the slot directory end.
    uint16_t new_slot_dir_end = PAGE_HEADER_SIZE
                              + (slot_count + 1) * SLOT_ENTRY_SIZE;
    uint16_t fso = get_free_space_offset();
    if (fso < length) return INVALID_SLOT_ID;  // arithmetic safety
    uint16_t new_fso = fso - length;
    if (new_fso < new_slot_dir_end) return INVALID_SLOT_ID;

    // Write the tuple data (grows backward from end of page).
    std::memcpy(data_ + new_fso, data, length);

    // Write the slot directory entry.
    set_slot(slot_count, new_fso, length);

    // Update header.
    set_slot_count(slot_count + 1);
    set_free_space_offset(new_fso);

    return slot_count;  // 0-indexed slot id
}

// ─── delete_tuple ───────────────────────────────────────────
bool Page::delete_tuple(slot_id_t slot_id) {
    if (slot_id >= get_slot_count()) return false;

    uint16_t offset, length;
    get_slot(slot_id, &offset, &length);
    if (length == 0) return false;  // already tombstoned

    // Tombstone: set length to 0 (offset is irrelevant now).
    set_slot(slot_id, offset, 0);
    return true;
}

// ─── get_tuple ──────────────────────────────────────────────
bool Page::get_tuple(slot_id_t slot_id, char* out, uint16_t* out_length) const {
    if (slot_id >= get_slot_count()) return false;

    uint16_t offset, length;
    get_slot(slot_id, &offset, &length);
    if (length == 0) return false;  // tombstoned

    std::memcpy(out, data_ + offset, length);
    *out_length = length;
    return true;
}

// ─── compact ────────────────────────────────────────────────
// Re-pack all live tuple data toward the end of the page, eliminating
// gaps left by deletions.  Tombstone slots stay as-is (length == 0).
void Page::compact() {
    uint16_t slot_count = get_slot_count();
    if (slot_count == 0) return;

    // Collect live (non-tombstoned) tuples.
    struct LiveTuple {
        slot_id_t slot_id;
        uint16_t  old_offset;
        uint16_t  length;
    };
    std::vector<LiveTuple> live;
    for (uint16_t i = 0; i < slot_count; ++i) {
        uint16_t off, len;
        get_slot(i, &off, &len);
        if (len > 0) {
            live.push_back({i, off, len});
        }
    }

    // Sort live tuples by their original offset descending (highest offset
    // = closest to end of page) so we can repack from the page end.
    std::sort(live.begin(), live.end(),
              [](const LiveTuple& a, const LiveTuple& b) {
                  return a.old_offset > b.old_offset;
              });

    // We need a temporary buffer to avoid overwrites during compaction.
    // Copy all live data into a temp buffer first.
    std::vector<char> temp;
    temp.reserve(PAGE_SIZE);
    // We'll store them in order: the one with the highest old_offset first.
    std::vector<uint16_t> lengths;
    for (auto& lt : live) {
        size_t pos = temp.size();
        temp.resize(pos + lt.length);
        std::memcpy(temp.data() + pos, data_ + lt.old_offset, lt.length);
        lengths.push_back(lt.length);
    }

    // Now write them back, packing from the end of the page.
    uint16_t write_offset = static_cast<uint16_t>(PAGE_SIZE);
    for (size_t i = 0; i < live.size(); ++i) {
        write_offset -= lengths[i];
        std::memcpy(data_ + write_offset, temp.data(),  lengths[i]);
        temp.erase(temp.begin(), temp.begin() + lengths[i]);
        set_slot(live[i].slot_id, write_offset, lengths[i]);
    }

    set_free_space_offset(write_offset);
}

// ═══════════════════════════════════════════════════════════════
//  Tuple Serialization / Deserialization
// ═══════════════════════════════════════════════════════════════

std::vector<char> serialize_tuple(const Tuple& tuple, const Schema& schema) {
    std::vector<char> buf;

    for (size_t i = 0; i < schema.columns.size(); ++i) {
        const Value& val = tuple.values[i];
        const Column& col = schema.columns[i];

        if (is_null(val)) {
            buf.push_back(1);  // null flag
            continue;
        }
        buf.push_back(0);  // not null

        switch (col.type) {
            case ColumnType::INT: {
                int v = std::get<int>(val);
                size_t pos = buf.size();
                buf.resize(pos + 4);
                std::memcpy(buf.data() + pos, &v, 4);
                break;
            }
            case ColumnType::FLOAT: {
                double v = std::get<double>(val);
                size_t pos = buf.size();
                buf.resize(pos + 8);
                std::memcpy(buf.data() + pos, &v, 8);
                break;
            }
            case ColumnType::BOOL: {
                bool v = std::get<bool>(val);
                buf.push_back(v ? 1 : 0);
                break;
            }
            case ColumnType::VARCHAR: {
                const std::string& s = std::get<std::string>(val);
                uint16_t len = static_cast<uint16_t>(s.size());
                size_t pos = buf.size();
                buf.resize(pos + 2 + len);
                std::memcpy(buf.data() + pos, &len, 2);
                std::memcpy(buf.data() + pos + 2, s.data(), len);
                break;
            }
        }
    }
    return buf;
}

Tuple deserialize_tuple(const char* data, uint16_t length, const Schema& schema) {
    Tuple tuple;
    uint16_t pos = 0;

    for (size_t i = 0; i < schema.columns.size(); ++i) {
        if (pos >= length) {
            throw std::runtime_error("deserialize_tuple: unexpected end of data");
        }
        uint8_t null_flag = static_cast<uint8_t>(data[pos++]);
        if (null_flag) {
            tuple.values.push_back(std::monostate{});
            continue;
        }

        const Column& col = schema.columns[i];
        switch (col.type) {
            case ColumnType::INT: {
                int v;
                std::memcpy(&v, data + pos, 4);
                pos += 4;
                tuple.values.push_back(v);
                break;
            }
            case ColumnType::FLOAT: {
                double v;
                std::memcpy(&v, data + pos, 8);
                pos += 8;
                tuple.values.push_back(v);
                break;
            }
            case ColumnType::BOOL: {
                bool v = data[pos++] != 0;
                tuple.values.push_back(v);
                break;
            }
            case ColumnType::VARCHAR: {
                uint16_t len;
                std::memcpy(&len, data + pos, 2);
                pos += 2;
                std::string s(data + pos, len);
                pos += len;
                tuple.values.push_back(std::move(s));
                break;
            }
        }
    }
    return tuple;
}

}  // namespace minidb
