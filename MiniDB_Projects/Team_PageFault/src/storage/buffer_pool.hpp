#pragma once
#include <vector>
#include <unordered_map>
#include <array>
#include "../common/config.hpp"
#include "../common/types.hpp"
#include "disk_manager.hpp"

namespace minidb {

// A fixed pool of in-memory page frames backed by the DiskManager. Eviction is
// clock-sweep: each frame carries a reference bit/counter; a "hand" sweeps the
// frames decrementing counters until it finds an unpinned frame with count 0,
// which it evicts (flushing first if dirty).
class BufferPool {
public:
    explicit BufferPool(DiskManager& disk, size_t frames = BUFFER_POOL_FRAMES);
    // Safety net: flush dirty frames on destruction so a caller that forgets
    // flushAll() doesn't silently lose data. (A simulated CRASH calls _Exit,
    // which skips destructors, so crash semantics are preserved.)
    ~BufferPool();

    // Pin and return a pointer to the page's bytes. Caller must unpin later.
    char* fetchPage(PageId id);

    // Allocate a fresh page on disk, pin it, and return its id + bytes.
    char* newPage(PageId& out_id);

    // Release a pin. If dirty, the frame is marked so it is written on eviction.
    void unpinPage(PageId id, bool dirty);

    // Force every dirty frame to disk (used on clean shutdown / durability).
    void flushAll();

    // Diagnostics for the demo / README.
    size_t numFrames() const { return frames_.size(); }

private:
    struct Frame {
        std::array<char, PAGE_SIZE> data{};
        PageId page = INVALID_PAGE;
        int pin = 0;
        bool dirty = false;
        uint8_t ref = 0;     // clock reference counter
        bool valid = false;
    };

    DiskManager& disk_;
    std::vector<Frame> frames_;
    std::unordered_map<PageId, size_t> table_; // page -> frame index
    size_t hand_ = 0;

    size_t pinFrameFor(PageId id, bool is_new);
    size_t evictVictim();
    void flushFrame(Frame& f);
};

} // namespace minidb
