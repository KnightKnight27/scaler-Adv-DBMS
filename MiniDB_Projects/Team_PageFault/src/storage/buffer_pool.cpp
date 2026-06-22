#include "buffer_pool.hpp"
#include <stdexcept>

namespace minidb {

BufferPool::BufferPool(DiskManager& disk, size_t frames) : disk_(disk) {
    frames_.resize(frames);
}

BufferPool::~BufferPool() {
    try { flushAll(); } catch (...) { /* never throw from a destructor */ }
}

void BufferPool::flushFrame(Frame& f) {
    if (f.valid && f.dirty) {
        disk_.writePage(f.page, f.data.data());
        f.dirty = false;
    }
}

// Clock-sweep: advance the hand, decrementing the reference counter on frames
// we pass, until we land on an unpinned frame whose counter is already 0.
size_t BufferPool::evictVictim() {
    size_t scanned = 0;
    size_t limit = frames_.size() * 4 + 1; // safety bound against livelock
    while (true) {
        Frame& f = frames_[hand_];
        if (!f.valid) {
            size_t idx = hand_;
            hand_ = (hand_ + 1) % frames_.size();
            return idx;
        }
        if (f.pin == 0) {
            if (f.ref == 0) {
                size_t idx = hand_;
                hand_ = (hand_ + 1) % frames_.size();
                flushFrame(f);
                table_.erase(f.page);
                f.valid = false;
                return idx;
            }
            f.ref--;
        }
        hand_ = (hand_ + 1) % frames_.size();
        if (++scanned > limit) throw std::runtime_error("buffer pool full: all frames pinned");
    }
}

size_t BufferPool::pinFrameFor(PageId id, bool is_new) {
    auto it = table_.find(id);
    if (it != table_.end()) {
        Frame& f = frames_[it->second];
        f.pin++;
        f.ref = 1; // touched
        return it->second;
    }
    size_t idx = evictVictim();
    Frame& f = frames_[idx];
    f.page = id;
    f.pin = 1;
    f.dirty = false;
    f.ref = 1;
    f.valid = true;
    if (is_new) f.data.fill(0);
    else disk_.readPage(id, f.data.data());
    table_[id] = idx;
    return idx;
}

char* BufferPool::fetchPage(PageId id) {
    size_t idx = pinFrameFor(id, /*is_new=*/false);
    return frames_[idx].data.data();
}

char* BufferPool::newPage(PageId& out_id) {
    out_id = disk_.allocatePage();
    size_t idx = pinFrameFor(out_id, /*is_new=*/true);
    return frames_[idx].data.data();
}

void BufferPool::unpinPage(PageId id, bool dirty) {
    auto it = table_.find(id);
    if (it == table_.end()) return;
    Frame& f = frames_[it->second];
    if (dirty) f.dirty = true;
    if (f.pin > 0) f.pin--;
}

void BufferPool::flushAll() {
    for (Frame& f : frames_) flushFrame(f);
}

} // namespace minidb
