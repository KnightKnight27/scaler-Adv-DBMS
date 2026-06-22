#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../recovery/wal.h"
#include "disk_manager.h"
#include "page.h"

namespace minidb {

// A fixed set of in-memory frames caching pages from disk. Pages are pinned
// while in use, evicted by least-recently-used order, and written back lazily.
class BufferPool {
public:
    BufferPool(DiskManager& dm, int pool_size = 128, LogManager* log = nullptr)
        : dm_(dm), log_(log), frames_(pool_size) {}

    void set_log(LogManager* log) { log_ = log; }

    Page fetch_page(int pid) {
        int f = find_frame(pid);
        if (f < 0) f = load(pid);
        frames_[f].pin++;
        frames_[f].used = ++tick_;
        return Page(frames_[f].data);
    }

    Page new_page(int& out_pid) {
        out_pid = dm_.allocate_page();
        int f = pick_victim();
        evict(f);
        Frame& fr = frames_[f];
        fr.page_id = out_pid;
        fr.dirty = false;
        fr.pin = 1;
        fr.used = ++tick_;
        std::memset(fr.data, 0, PAGE_SIZE);
        Page p(fr.data);
        p.init();
        table_[out_pid] = f;
        return p;
    }

    void unpin(int pid, bool dirty) {
        int f = find_frame(pid);
        if (f < 0) return;
        if (dirty) frames_[f].dirty = true;
        if (frames_[f].pin > 0) frames_[f].pin--;
    }

    void set_lsn(int pid, int64_t lsn) {
        int f = find_frame(pid);
        if (f >= 0) Page(frames_[f].data).set_lsn(lsn);
    }

    void flush_all() {
        for (size_t f = 0; f < frames_.size(); ++f)
            if (frames_[f].page_id >= 0 && frames_[f].dirty) write_frame(static_cast<int>(f));
    }

    // Drop every cached page without writing it back. Used by tests/demos to
    // emulate a crash: only what reached disk (and the log) survives.
    void discard_all() {
        for (auto& fr : frames_) {
            fr.page_id = -1;
            fr.dirty = false;
            fr.pin = 0;
        }
        table_.clear();
    }

private:
    struct Frame {
        int page_id = -1;
        bool dirty = false;
        int pin = 0;
        uint64_t used = 0;
        uint8_t data[PAGE_SIZE];
    };

    DiskManager& dm_;
    LogManager* log_;
    std::vector<Frame> frames_;
    std::unordered_map<int, int> table_;
    uint64_t tick_ = 0;

    int find_frame(int pid) {
        auto it = table_.find(pid);
        return it == table_.end() ? -1 : it->second;
    }

    int pick_victim() {
        int best = -1;
        uint64_t best_used = UINT64_MAX;
        for (size_t f = 0; f < frames_.size(); ++f) {
            if (frames_[f].page_id < 0) return static_cast<int>(f);
            if (frames_[f].pin == 0 && frames_[f].used < best_used) {
                best_used = frames_[f].used;
                best = static_cast<int>(f);
            }
        }
        if (best < 0) throw std::runtime_error("buffer pool exhausted: all frames pinned");
        return best;
    }

    void evict(int f) {
        if (frames_[f].page_id >= 0) {
            if (frames_[f].dirty) write_frame(f);
            table_.erase(frames_[f].page_id);
        }
    }

    int load(int pid) {
        int f = pick_victim();
        evict(f);
        Frame& fr = frames_[f];
        fr.page_id = pid;
        fr.dirty = false;
        fr.pin = 0;
        fr.used = ++tick_;
        dm_.read_page(pid, fr.data);
        table_[pid] = f;
        return f;
    }

    void write_frame(int f) {
        Frame& fr = frames_[f];
        if (log_) log_->flush(Page(fr.data).lsn());  // write-ahead rule
        dm_.write_page(fr.page_id, fr.data);
        fr.dirty = false;
    }
};

}  // namespace minidb
