#include "storage.h"

#include <stdexcept>

namespace minidb {

// ---------------------------------------------------------------------------
// big-endian helpers
// ---------------------------------------------------------------------------
static void putU16(std::string& b, int pos, int v) {
    b[pos] = static_cast<char>((v >> 8) & 0xFF);
    b[pos + 1] = static_cast<char>(v & 0xFF);
}
static int getU16(const std::string& b, int pos) {
    return ((static_cast<unsigned char>(b[pos]) << 8) |
            static_cast<unsigned char>(b[pos + 1]));
}

// ---------------------------------------------------------------------------
// row encoding
// ---------------------------------------------------------------------------
std::string encodeRow(const Row& row) {
    std::string out;
    for (const Value& v : row) {
        if (v.isInt()) {
            out.push_back(0x01);
            uint64_t u = static_cast<uint64_t>(v.i);
            for (int shift = 56; shift >= 0; shift -= 8)
                out.push_back(static_cast<char>((u >> shift) & 0xFF));
        } else {
            out.push_back(0x02);
            int len = static_cast<int>(v.s.size());
            std::string hdr(2, '\0');
            putU16(hdr, 0, len);
            out += hdr;
            out += v.s;
        }
    }
    return out;
}

Row decodeRow(const std::string& bytes) {
    Row row;
    size_t i = 0;
    while (i < bytes.size()) {
        unsigned char tag = static_cast<unsigned char>(bytes[i++]);
        if (tag == 0x01) {
            uint64_t u = 0;
            for (int k = 0; k < 8; ++k)
                u = (u << 8) | static_cast<unsigned char>(bytes[i++]);
            row.push_back(Value::Int(static_cast<int64_t>(u)));
        } else if (tag == 0x02) {
            int len = getU16(bytes, static_cast<int>(i));
            i += 2;
            row.push_back(Value::Text(bytes.substr(i, len)));
            i += len;
        } else {
            break;
        }
    }
    return row;
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------
Page::Page(const std::string& bytes) : data_(bytes) {
    if (static_cast<int>(data_.size()) != PAGE_SIZE) data_.resize(PAGE_SIZE, '\0');
}

Page Page::empty() {
    std::string b(PAGE_SIZE, '\0');
    Page p(b);
    p.setNumSlots(0);
    p.setFreePtr(PAGE_SIZE);
    return p;
}

int Page::numSlots() const { return getU16(data_, 0); }
void Page::setNumSlots(int n) { putU16(data_, 0, n); }
int Page::freePtr() const { return getU16(data_, 2); }
void Page::setFreePtr(int p) { putU16(data_, 2, p); }

void Page::slot(int i, int& offset, int& length) const {
    int base = HEADER_SIZE + i * SLOT_SIZE;
    offset = getU16(data_, base);
    length = getU16(data_, base + 2);
}
void Page::setSlot(int i, int offset, int length) {
    int base = HEADER_SIZE + i * SLOT_SIZE;
    putU16(data_, base, offset);
    putU16(data_, base + 2, length);
}

int Page::freeSpace() const {
    return freePtr() - (HEADER_SIZE + numSlots() * SLOT_SIZE);
}

int Page::insert(const std::string& record) {
    int len = static_cast<int>(record.size());
    if (freeSpace() < len + SLOT_SIZE) return -1;
    int slotId = numSlots();
    int newFree = freePtr() - len;
    for (int k = 0; k < len; ++k) data_[newFree + k] = record[k];
    setSlot(slotId, newFree, len);
    setFreePtr(newFree);
    setNumSlots(slotId + 1);
    return slotId;
}

bool Page::get(int slotId, std::string& out) const {
    if (slotId >= numSlots()) return false;
    int offset, length;
    slot(slotId, offset, length);
    if (length == 0) return false;  // deleted
    out = data_.substr(offset, length);
    return true;
}

void Page::erase(int slotId) {
    int offset, length;
    slot(slotId, offset, length);
    setSlot(slotId, offset, 0);  // tombstone; bytes are not reclaimed
}

// ---------------------------------------------------------------------------
// DiskManager
// ---------------------------------------------------------------------------
DiskManager::DiskManager(const std::string& path) : path_(path) {
    // Make sure the file exists, then open for read+write in binary mode.
    std::fstream probe(path_, std::ios::in | std::ios::binary);
    if (!probe.is_open()) {
        std::ofstream create(path_, std::ios::binary);
        create.close();
    }
    f_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!f_.is_open()) throw std::runtime_error("cannot open table file: " + path_);
}

int DiskManager::numPages() {
    f_.seekg(0, std::ios::end);
    return static_cast<int>(f_.tellg()) / PAGE_SIZE;
}

std::string DiskManager::readPage(int pageId) {
    f_.clear();
    f_.seekg(static_cast<std::streamoff>(pageId) * PAGE_SIZE, std::ios::beg);
    std::string buf(PAGE_SIZE, '\0');
    f_.read(&buf[0], PAGE_SIZE);
    return buf;  // short reads leave the tail zero-filled, which is fine
}

void DiskManager::writePage(int pageId, const std::string& data) {
    f_.clear();
    f_.seekp(static_cast<std::streamoff>(pageId) * PAGE_SIZE, std::ios::beg);
    f_.write(data.data(), PAGE_SIZE);
    f_.flush();  // push to the OS for durability
}

int DiskManager::allocatePage() {
    // A new page must be a *formatted* empty page (free_ptr = PAGE_SIZE);
    // a plain zeroed block would look permanently full.
    int pageId = numPages();
    writePage(pageId, Page::empty().bytes());
    return pageId;
}

// ---------------------------------------------------------------------------
// BufferPool
// ---------------------------------------------------------------------------
BufferPool::BufferPool(DiskManager* disk, int capacity)
    : disk_(disk), capacity_(capacity) {}

void BufferPool::touch(int pageId) {
    auto it = pos_.find(pageId);
    if (it != pos_.end()) lru_.erase(it->second);
    lru_.push_back(pageId);
    pos_[pageId] = std::prev(lru_.end());
}

void BufferPool::evictOne() {
    int victim = lru_.front();
    lru_.pop_front();
    pos_.erase(victim);
    if (dirty_.count(victim)) {
        disk_->writePage(victim, frames_.at(victim).bytes());
        dirty_.erase(victim);
    }
    frames_.erase(victim);
    stats.evictions++;
}

Page* BufferPool::fetch(int pageId) {
    auto it = frames_.find(pageId);
    if (it != frames_.end()) {
        stats.hits++;
        touch(pageId);
        return &it->second;
    }
    stats.misses++;
    if (static_cast<int>(frames_.size()) >= capacity_) evictOne();
    frames_.emplace(pageId, Page(disk_->readPage(pageId)));
    touch(pageId);
    return &frames_.at(pageId);
}

void BufferPool::markDirty(int pageId) { dirty_.insert(pageId); }

void BufferPool::flushAll() {
    for (int pageId : dirty_)
        disk_->writePage(pageId, frames_.at(pageId).bytes());
    dirty_.clear();
}

// ---------------------------------------------------------------------------
// HeapFile
// ---------------------------------------------------------------------------
RID HeapFile::insert(const std::string& record) {
    int need = static_cast<int>(record.size()) + SLOT_SIZE;
    int n = disk_->numPages();
    // Append to the last page if it has room, else grow by one page. This
    // keeps insert O(1) instead of rescanning every page. Space freed by
    // deletes in earlier pages is not reused (documented trade-off).
    if (n > 0) {
        Page* last = pool_->fetch(n - 1);
        if (last->freeSpace() >= need) {
            int slot = last->insert(record);
            pool_->markDirty(n - 1);
            return RID{n - 1, slot};
        }
    }
    int pageId = disk_->allocatePage();
    Page* page = pool_->fetch(pageId);
    int slot = page->insert(record);
    pool_->markDirty(pageId);
    return RID{pageId, slot};
}

bool HeapFile::get(const RID& rid, std::string& out) {
    Page* page = pool_->fetch(rid.page);
    return page->get(rid.slot, out);
}

void HeapFile::erase(const RID& rid) {
    Page* page = pool_->fetch(rid.page);
    page->erase(rid.slot);
    pool_->markDirty(rid.page);
}

}  // namespace minidb
