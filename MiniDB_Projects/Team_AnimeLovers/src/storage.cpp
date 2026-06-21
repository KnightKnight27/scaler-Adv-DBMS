#include "storage.h"
#include <stdexcept>
#include <algorithm>

// ─── DiskManager ─────────────────────────────────────────────────────────────

DiskManager::DiskManager(const std::string& filepath) {
    // Open for read+write in binary mode; create if absent
    file_.open(filepath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        // Create the file first, then re-open r/w
        std::fstream create(filepath, std::ios::out | std::ios::binary);
        create.close();
        file_.open(filepath, std::ios::in | std::ios::out | std::ios::binary);
    }
    // Determine existing page count from file size
    file_.seekg(0, std::ios::end);
    auto bytes = static_cast<uint64_t>(file_.tellg());
    page_count_ = static_cast<uint32_t>(bytes / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (file_.is_open()) file_.close();
}

void DiskManager::read_page(PageId pid, Page& page) {
    if (pid >= page_count_) throw std::runtime_error("read_page: invalid pid");
    file_.seekg(static_cast<std::streamoff>(pid) * PAGE_SIZE, std::ios::beg);
    file_.read(reinterpret_cast<char*>(page.data.data()), PAGE_SIZE);
}

void DiskManager::write_page(PageId pid, const Page& page) {
    if (pid >= page_count_) throw std::runtime_error("write_page: invalid pid");
    file_.seekp(static_cast<std::streamoff>(pid) * PAGE_SIZE, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(page.data.data()), PAGE_SIZE);
    file_.flush();
}

PageId DiskManager::allocate_page() {
    // Extend the file by one blank page and return its id
    PageId new_pid = page_count_++;
    file_.seekp(static_cast<std::streamoff>(new_pid) * PAGE_SIZE, std::ios::beg);
    std::array<uint8_t, PAGE_SIZE> blank{};
    file_.write(reinterpret_cast<const char*>(blank.data()), PAGE_SIZE);
    file_.flush();
    return new_pid;
}

// ─── BufferPool ───────────────────────────────────────────────────────────────

BufferPool::BufferPool(DiskManager& dm, size_t pool_size)
    : dm_(dm), frames_(pool_size) {
    // Initialise LRU list with all frame indices (all are free at start)
    for (size_t i = 0; i < pool_size; ++i) {
        lru_list_.push_back(i);
        lru_pos_[i] = std::prev(lru_list_.end());
    }
}

Page* BufferPool::fetch(PageId pid) {
    // Cache hit: move to front of LRU and increment pin count
    if (auto it = page_to_frame_.find(pid); it != page_to_frame_.end()) {
        size_t fi = it->second;
        // Move to front of LRU (most recently used)
        lru_list_.erase(lru_pos_[fi]);
        lru_list_.push_front(fi);
        lru_pos_[fi] = lru_list_.begin();
        frames_[fi].pin_cnt++;
        return &frames_[fi].page;
    }

    // Cache miss: need a free frame
    size_t fi = evict();
    Frame& frame = frames_[fi];

    // Read the requested page from disk
    if (pid < dm_.page_count()) {
        dm_.read_page(pid, frame.page);
    } else {
        // New page that hasn't been written yet — initialise it
        frame.page.init();
    }
    frame.pid     = pid;
    frame.pin_cnt = 1;
    frame.dirty   = false;
    page_to_frame_[pid] = fi;

    lru_list_.erase(lru_pos_[fi]);
    lru_list_.push_front(fi);
    lru_pos_[fi] = lru_list_.begin();

    return &frame.page;
}

void BufferPool::unpin(PageId pid, bool dirty) {
    auto it = page_to_frame_.find(pid);
    if (it == page_to_frame_.end()) return;
    Frame& frame = frames_[it->second];
    if (frame.pin_cnt > 0) frame.pin_cnt--;
    if (dirty) frame.dirty = true;
}

void BufferPool::flush_all() {
    for (auto& frame : frames_) {
        if (frame.pid != INVALID_PID && frame.dirty) {
            dm_.write_page(frame.pid, frame.page);
            frame.dirty = false;
        }
    }
}

size_t BufferPool::evict() {
    // Walk LRU from back (least recently used) until we find an unpinned frame
    for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
        size_t fi = *it;
        Frame& frame = frames_[fi];
        if (frame.pin_cnt > 0) continue; // still in use

        // Write back if dirty before evicting
        if (frame.dirty && frame.pid != INVALID_PID) {
            dm_.write_page(frame.pid, frame.page);
            frame.dirty = false;
        }
        if (frame.pid != INVALID_PID)
            page_to_frame_.erase(frame.pid);

        frame.pid = INVALID_PID;
        return fi;
    }
    throw std::runtime_error("BufferPool: all frames pinned — pool too small");
}

// ─── Serialization ────────────────────────────────────────────────────────────

namespace serde {

static void push_u8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}
static void push_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}
static void push_i64(std::vector<uint8_t>& buf, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) buf.push_back((u >> (8*i)) & 0xFF);
}

std::vector<uint8_t> encode(const std::vector<Value>& row) {
    std::vector<uint8_t> buf;
    for (const Value& v : row) {
        push_u8(buf, static_cast<uint8_t>(v.type));
        push_u8(buf, v.is_null ? 1 : 0);
        if (!v.is_null) {
            if (v.type == Type::INT) {
                push_i64(buf, v.as_int());
            } else {
                const std::string& s = v.as_str();
                push_u32(buf, static_cast<uint32_t>(s.size()));
                buf.insert(buf.end(), s.begin(), s.end());
            }
        }
    }
    return buf;
}

std::vector<Value> decode(const uint8_t* p, size_t /*len*/, const std::vector<Type>& types) {
    std::vector<Value> row;
    for (Type t : types) {
        uint8_t type_tag = *p++;
        (void)type_tag; // we trust the schema type
        bool is_null = (*p++ != 0);
        if (is_null) {
            row.push_back(Value::make_null(t));
        } else if (t == Type::INT) {
            int64_t v = 0;
            for (int i = 0; i < 8; ++i) v |= (int64_t)(*p++) << (8*i);
            row.push_back(Value::make_int(v));
        } else {
            uint32_t len = 0;
            for (int i = 0; i < 4; ++i) len |= (uint32_t)(*p++) << (8*i);
            std::string s(reinterpret_cast<const char*>(p), len);
            p += len;
            row.push_back(Value::make_varchar(std::move(s)));
        }
    }
    return row;
}

} // namespace serde
