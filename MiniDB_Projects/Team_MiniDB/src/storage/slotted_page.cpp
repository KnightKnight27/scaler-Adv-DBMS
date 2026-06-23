#include "storage/slotted_page.h"

#include <cstring>
#include <vector>

namespace minidb {

// Fixed header field offsets (see the layout comment in slotted_page.h).
namespace {
constexpr std::size_t OFF_PAGE_LSN  = 4;
constexpr std::size_t OFF_NEXT_PAGE = 12;
constexpr std::size_t OFF_SLOT_CNT  = 16;
constexpr std::size_t OFF_FREE_PTR  = 18;

// memcpy-based raw readers/writers avoid unaligned-access undefined behaviour.
template <typename T>
T read_at(const char* p, std::size_t off) {
    T v;
    std::memcpy(&v, p + off, sizeof(T));
    return v;
}
template <typename T>
void write_at(char* p, std::size_t off, T v) {
    std::memcpy(p + off, &v, sizeof(T));
}
} // namespace

void SlottedPage::init() {
    set_page_lsn(INVALID_LSN);
    set_next_page(INVALID_PAGE_ID);
    set_slot_count(0);
    set_free_ptr(static_cast<std::uint16_t>(PAGE_SIZE));
    // checksum (bytes [0,4)) is stamped by the DiskManager at write time.
    write_at<std::uint32_t>(data_, 0, 0);
}

lsn_t  SlottedPage::page_lsn() const         { return read_at<lsn_t>(data_, OFF_PAGE_LSN); }
void   SlottedPage::set_page_lsn(lsn_t lsn)  { write_at<lsn_t>(data_, OFF_PAGE_LSN, lsn); }
PageId SlottedPage::next_page() const        { return read_at<PageId>(data_, OFF_NEXT_PAGE); }
void   SlottedPage::set_next_page(PageId id) { write_at<PageId>(data_, OFF_NEXT_PAGE, id); }

std::uint16_t SlottedPage::slot_count() const          { return read_at<std::uint16_t>(data_, OFF_SLOT_CNT); }
void          SlottedPage::set_slot_count(std::uint16_t n) { write_at<std::uint16_t>(data_, OFF_SLOT_CNT, n); }
std::uint16_t SlottedPage::free_ptr() const            { return read_at<std::uint16_t>(data_, OFF_FREE_PTR); }
void          SlottedPage::set_free_ptr(std::uint16_t p)   { write_at<std::uint16_t>(data_, OFF_FREE_PTR, p); }

SlottedPage::Slot SlottedPage::slot_at(std::int16_t i) const {
    std::size_t base = HEADER_SIZE + static_cast<std::size_t>(i) * SLOT_SIZE;
    return Slot{ read_at<std::uint16_t>(data_, base),
                read_at<std::uint16_t>(data_, base + 2) };
}

void SlottedPage::set_slot(std::int16_t i, Slot s) {
    std::size_t base = HEADER_SIZE + static_cast<std::size_t>(i) * SLOT_SIZE;
    write_at<std::uint16_t>(data_, base, s.offset);
    write_at<std::uint16_t>(data_, base + 2, s.length);
}

std::uint16_t SlottedPage::free_space() const {
    std::uint16_t slot_dir_end =
        HEADER_SIZE + static_cast<std::uint16_t>(slot_count() * SLOT_SIZE);
    std::uint16_t fp = free_ptr();
    return fp > slot_dir_end ? static_cast<std::uint16_t>(fp - slot_dir_end) : 0;
}

bool SlottedPage::insert(const char* rec, std::uint16_t len, std::int16_t& out_slot) {
    // Try to reuse an erased slot so the directory does not grow unnecessarily.
    std::int16_t reuse = -1;
    std::uint16_t n = slot_count();
    for (std::uint16_t i = 0; i < n; ++i) {
        Slot s = slot_at(static_cast<std::int16_t>(i));
        if (s.offset == 0 && s.length == 0) { reuse = static_cast<std::int16_t>(i); break; }
    }

    // Space needed: the record bytes, plus one slot entry if we must grow the directory.
    std::uint16_t need = len + (reuse == -1 ? SLOT_SIZE : 0);
    if (free_space() < need) {
        compact();
        if (free_space() < need) return false;  // genuinely full
    }

    std::uint16_t new_off = static_cast<std::uint16_t>(free_ptr() - len);
    std::memcpy(data_ + new_off, rec, len);
    set_free_ptr(new_off);

    if (reuse != -1) {
        set_slot(reuse, Slot{new_off, len});
        out_slot = reuse;
    } else {
        set_slot(static_cast<std::int16_t>(n), Slot{new_off, len});
        set_slot_count(static_cast<std::uint16_t>(n + 1));
        out_slot = static_cast<std::int16_t>(n);
    }
    return true;
}

bool SlottedPage::get(std::int16_t slot, std::string& out) const {
    if (slot < 0 || slot >= static_cast<std::int16_t>(slot_count())) return false;
    Slot s = slot_at(slot);
    if (s.offset == 0 && s.length == 0) return false;  // erased
    out.assign(data_ + s.offset, s.length);
    return true;
}

bool SlottedPage::erase(std::int16_t slot) {
    if (slot < 0 || slot >= static_cast<std::int16_t>(slot_count())) return false;
    Slot s = slot_at(slot);
    if (s.offset == 0 && s.length == 0) return false;  // already erased
    set_slot(slot, Slot{0, 0});
    return true;
}

void SlottedPage::compact() {
    // Rebuild the record region bottom-up, keeping only live records, and rewrite
    // each live slot's offset. Slot indices are preserved so RIDs stay valid.
    std::uint16_t n = slot_count();
    std::vector<char> buf(PAGE_SIZE);
    std::uint16_t write_ptr = static_cast<std::uint16_t>(PAGE_SIZE);

    for (std::uint16_t i = 0; i < n; ++i) {
        Slot s = slot_at(static_cast<std::int16_t>(i));
        if (s.offset == 0 && s.length == 0) continue;  // skip erased
        write_ptr = static_cast<std::uint16_t>(write_ptr - s.length);
        std::memcpy(buf.data() + write_ptr, data_ + s.offset, s.length);
        set_slot(static_cast<std::int16_t>(i), Slot{write_ptr, s.length});
    }
    // Copy the compacted record region back into the page and reset free_ptr.
    std::memcpy(data_ + write_ptr, buf.data() + write_ptr, PAGE_SIZE - write_ptr);
    set_free_ptr(write_ptr);
}

} // namespace minidb
