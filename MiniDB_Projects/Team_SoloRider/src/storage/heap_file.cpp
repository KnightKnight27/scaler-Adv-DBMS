// storage/heap_file.cpp — Heap file implementation.
//
// The file stores a small header (4 bytes = num_pages) followed by a
// sequence of fixed-size pages.  An in-memory free-space map accelerates
// inserts by avoiding full-page scans.

#include "storage/heap_file.h"

#include <filesystem>
#include <stdexcept>

namespace minidb {

// ─── Constructor ────────────────────────────────────────────
HeapFile::HeapFile(const std::string& file_path)
    : file_path_(file_path), num_pages_(0)
{
    // Check if the file already exists and has content.
    bool exists = std::filesystem::exists(file_path_)
               && std::filesystem::file_size(file_path_) > 0;

    if (exists) {
        // Open existing file for read+write in binary mode.
        file_.open(file_path_,
                   std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("HeapFile: cannot open " + file_path_);
        }
        read_header();
        rebuild_free_space_map();
    } else {
        // Create a new file.
        file_.open(file_path_,
                   std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file_.is_open()) {
            throw std::runtime_error("HeapFile: cannot create " + file_path_);
        }
        num_pages_ = 0;
        write_header();
        file_.flush();
    }
}

HeapFile::~HeapFile() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

// ─── Header I/O ─────────────────────────────────────────────
void HeapFile::write_header() {
    file_.seekp(0, std::ios::beg);
    uint32_t np = num_pages_;
    file_.write(reinterpret_cast<const char*>(&np), sizeof(np));
    file_.flush();
}

void HeapFile::read_header() {
    file_.seekg(0, std::ios::beg);
    uint32_t np = 0;
    file_.read(reinterpret_cast<char*>(&np), sizeof(np));
    num_pages_ = np;
}

// ─── Page management ───────────────────────────────────────
page_id_t HeapFile::allocate_page() {
    page_id_t new_pid = num_pages_;

    // Write a blank page at the end of the file.
    Page blank;
    blank.set_page_id(new_pid);

    file_.seekp(page_offset(new_pid), std::ios::beg);
    file_.write(blank.get_data(), PAGE_SIZE);
    file_.flush();

    num_pages_++;
    write_header();

    // Add entry to the free-space map.
    free_space_map_.push_back(blank.get_free_space());

    return new_pid;
}

void HeapFile::read_page(page_id_t pid, Page& page) {
    if (pid >= num_pages_) {
        throw std::runtime_error("HeapFile::read_page: page_id out of range");
    }
    file_.seekg(page_offset(pid), std::ios::beg);
    file_.read(page.get_data(), PAGE_SIZE);
}

void HeapFile::write_page(page_id_t pid, const Page& page) {
    if (pid >= num_pages_) {
        throw std::runtime_error("HeapFile::write_page: page_id out of range");
    }
    file_.seekp(page_offset(pid), std::ios::beg);
    file_.write(page.get_data(), PAGE_SIZE);
    file_.flush();
}

// ─── Tuple-level API ───────────────────────────────────────
RecordId HeapFile::insert_tuple(const char* data, uint16_t length) {
    // Scan the free-space map for a page with enough room.
    for (page_id_t pid = 0; pid < num_pages_; ++pid) {
        if (free_space_map_[pid] >= length) {
            Page page;
            read_page(pid, page);
            slot_id_t sid = page.insert_tuple(data, length);
            if (sid != INVALID_SLOT_ID) {
                write_page(pid, page);
                free_space_map_[pid] = page.get_free_space();
                return RecordId{pid, sid};
            }
            // The free-space map was slightly optimistic; update it.
            free_space_map_[pid] = page.get_free_space();
        }
    }

    // No existing page has room — allocate a new one.
    page_id_t new_pid = allocate_page();
    Page page;
    read_page(new_pid, page);
    slot_id_t sid = page.insert_tuple(data, length);
    if (sid == INVALID_SLOT_ID) {
        throw std::runtime_error("HeapFile: tuple too large for a single page");
    }
    write_page(new_pid, page);
    free_space_map_[new_pid] = page.get_free_space();
    return RecordId{new_pid, sid};
}

bool HeapFile::delete_tuple(RecordId rid) {
    if (rid.page_id >= num_pages_) return false;

    Page page;
    read_page(rid.page_id, page);
    bool ok = page.delete_tuple(rid.slot_id);
    if (ok) {
        write_page(rid.page_id, page);
        free_space_map_[rid.page_id] = page.get_free_space();
    }
    return ok;
}

bool HeapFile::get_tuple(RecordId rid, char* out, uint16_t* out_length) {
    if (rid.page_id >= num_pages_) return false;

    Page page;
    read_page(rid.page_id, page);
    return page.get_tuple(rid.slot_id, out, out_length);
}

// ─── Free-space map rebuild ─────────────────────────────────
void HeapFile::rebuild_free_space_map() {
    free_space_map_.clear();
    free_space_map_.resize(num_pages_);
    Page page;
    for (page_id_t pid = 0; pid < num_pages_; ++pid) {
        read_page(pid, page);
        free_space_map_[pid] = page.get_free_space();
    }
}

}  // namespace minidb
