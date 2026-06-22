#pragma once

#include "common/types.h"
#include <cstring>
#include <memory>
#include <array>
#include <algorithm>

namespace minidb {

// page — 4 kb fixed-size disk page
// layout (4 096 bytes):
//   [0..3]   page_id          (uint32_t)
//   [4..7]   record_count     (uint32_t)
//   [8..11]  free_space_bot   (uint32_t) — next free byte from bottom
//   [12..15] free_space_top   (uint32_t) — next free byte from top  (unused simplified)
//   [16]     page_type        (uint8_t)  — 0=data, 1=index internal, 2=index leaf
//   [17]     dirty            (uint8_t)  — in-memory only
//   [18]     pin_count        (uint16_t) — in-memory only
//   [20..23] reserved
//   [24..4095] slot directory grows →  [slot_0, slot_1, ...]
//             records grow ← from end

constexpr size_t PAGE_HEADER_SIZE = 24;
constexpr size_t PAGE_DATA_SIZE   = PAGE_SIZE - PAGE_HEADER_SIZE;

enum class PageType : uint8_t { DATA = 0, INDEX_INTERNAL = 1, INDEX_LEAF = 2 };

// each slot describes one record stored inside the page.
struct Slot {
    uint32_t offset;   // byte offset from page start
    uint32_t length;   // record length in bytes
};

class Page {
public:
    Page();
    explicit Page(PageID id);

    // --- persistent fields -------------------------------------------------
    PageID page_id() const { return _page_id; }
    void set_page_id(PageID id) { _page_id = id; }

    PageType page_type() const { return _type; }
    void set_page_type(PageType t) { _type = t; }

    uint32_t record_count() const { return _record_count; }

    // --- in-memory fields --------------------------------------------------
    bool dirty() const { return _dirty; }
    void set_dirty(bool v) { _dirty = v; }

    uint16_t pin_count() const { return _pin_count; }
    void pin()   { ++_pin_count; }
    void unpin() { if (_pin_count) --_pin_count; }

    // --- slot operations ---------------------------------------------------
    // insert a record; returns slot index or -1 if page is full.
    int insert_record(const Record& rec);
    // read record at slot index.
    Record get_record(int slot_idx) const;
    // delete record at slot index (compact if needed).
    bool delete_record(int slot_idx);

    // --- serialisation -----------------------------------------------------
    // write page contents into a binary buffer (excl. in-memory fields).
    void serialize(char* buf) const;
    // read page contents from a binary buffer.
    void deserialize(const char* buf);

    // --- raw access --------------------------------------------------------
    const char* data()    const { return _data; }
    char*       data_mut()       { return _data; }

    // --- slot directory access ---------------------------------------------
    const std::vector<Slot>& slots() const { return _slots; }

private:
    // --- serialised fields ------------------------------------------------
    PageID   _page_id = INVALID_PAGE;
    uint32_t _record_count = 0;
    uint32_t _free_bot     = PAGE_HEADER_SIZE;  // next free byte (grows upward)
    PageType _type         = PageType::DATA;

    // --- in-memory only ---------------------------------------------------
    bool     _dirty    = false;
    uint16_t _pin_count = 0;

    // --- runtime structures ------------------------------------------------
    std::vector<Slot> _slots;           // slot directory
    char              _data[PAGE_SIZE]; // raw page bytes
};

// page implementation (inline for simplicity)

inline Page::Page() { std::memset(_data, 0, PAGE_SIZE); }

inline Page::Page(PageID id) : _page_id(id) { std::memset(_data, 0, PAGE_SIZE); }

inline int Page::insert_record(const Record& rec) {
    // serialise record into a temp buffer
    std::ostringstream oss(std::ios::binary);
    write_record(oss, rec);
    std::string bytes = oss.str();
    uint32_t len = static_cast<uint32_t>(bytes.size());

    // need: slot entry (8 bytes) + record bytes
    uint32_t needed = static_cast<uint32_t>(sizeof(Slot)) + len;
    uint32_t available = PAGE_SIZE - _free_bot;
    if (needed > available) return -1;

    // place record at free_bot
    std::memcpy(_data + _free_bot, bytes.data(), len);
    Slot slot{ _free_bot, len };
    _slots.push_back(slot);
    _free_bot += len;
    _record_count = static_cast<uint32_t>(_slots.size());
    _dirty = true;
    return static_cast<int>(_slots.size() - 1);
}

inline Record Page::get_record(int slot_idx) const {
    if (slot_idx < 0 || static_cast<size_t>(slot_idx) >= _slots.size())
        return {};
    const auto& slot = _slots[slot_idx];
    std::string bytes(_data + slot.offset, slot.length);
    std::istringstream iss(bytes, std::ios::binary);
    return read_record(iss);
}

inline bool Page::delete_record(int slot_idx) {
    if (slot_idx < 0 || static_cast<size_t>(slot_idx) >= _slots.size())
        return false;
    _slots.erase(_slots.begin() + slot_idx);
    _record_count = static_cast<uint32_t>(_slots.size());
    _dirty = true;
    return true;
}

inline void Page::serialize(char* buf) const {
    std::memcpy(buf, &_page_id, sizeof(_page_id));
    std::memcpy(buf + 4, &_record_count, sizeof(_record_count));
    std::memcpy(buf + 8, &_free_bot, sizeof(_free_bot));
    // free_space_top unused, keep zero
    buf[16] = static_cast<char>(_type);
    // skip in-memory fields
    std::memcpy(buf + 24, _data + PAGE_HEADER_SIZE, PAGE_DATA_SIZE);
}

inline void Page::deserialize(const char* buf) {
    std::memcpy(&_page_id, buf, sizeof(_page_id));
    std::memcpy(&_record_count, buf + 4, sizeof(_record_count));
    std::memcpy(&_free_bot, buf + 8, sizeof(_free_bot));
    _type = static_cast<PageType>(buf[16]);
    std::memcpy(_data + PAGE_HEADER_SIZE, buf + PAGE_HEADER_SIZE, PAGE_DATA_SIZE);
    // rebuild slot directory by scanning
    _slots.clear();
    uint32_t pos = PAGE_HEADER_SIZE;
    for (uint32_t i = 0; i < _record_count && pos < _free_bot; ++i) {
        std::string bytes(_data + pos, 4);
        // read record length first
        std::istringstream iss(std::string(_data + pos, _free_bot - pos), std::ios::binary);
        auto saved_pos = iss.tellg();
        uint32_t cols = read_u32(iss);
        // skip through values to find length
        for (uint32_t c = 0; c < cols; ++c) {
            auto vt = static_cast<ValueType>(iss.get());
            switch (vt) {
                case ValueType::INT32:   iss.ignore(4); break;
                case ValueType::FLOAT64: iss.ignore(8); break;
                case ValueType::STRING: {
                    uint32_t slen = read_u32(iss);
                    iss.ignore(slen); break;
                }
                default: break;
            }
        }
        uint32_t rec_len = static_cast<uint32_t>(iss.tellg()) - static_cast<uint32_t>(saved_pos);
        _slots.push_back({pos, rec_len});
        pos += rec_len;
    }
}

} // namespace minidb
