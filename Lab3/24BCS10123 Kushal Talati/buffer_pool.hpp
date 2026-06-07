// Lab 3 — Clock-Sweep Buffer Pool
// 24BCS10123  Kushal Talati
//
// Header-only, generic page cache modelled on PostgreSQL's
// src/backend/storage/buffer/freelist.c. Frames carry a small reference
// counter that decays as the clock hand walks past; pinned frames are
// invisible to the sweep. The API is shaped so the pool is responsible
// for fetching a missing page (caller supplies a loader callback), which
// mirrors how real buffer managers are written.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace kt {

// Postgres uses BM_MAX_USAGE_COUNT = 5; we follow the same upper bound.
inline constexpr int kRefBitsCap = 5;

template <typename PageId, typename Page>
class BufferPool {
public:
    using Loader = std::function<Page(const PageId&)>;
    using Writer = std::function<void(const PageId&, const Page&)>;

    struct Metrics {
        std::uint64_t hits         = 0;
        std::uint64_t misses       = 0;
        std::uint64_t evictions    = 0;
        std::uint64_t hand_visits  = 0;   // increments every time the hand crosses a frame
        std::uint64_t hand_rounds  = 0;   // full revolutions completed by the hand
    };

    explicit BufferPool(std::size_t capacity)
        : capacity_(capacity) {
        if (capacity == 0) throw std::invalid_argument("BufferPool: capacity must be > 0");
        slots_.reserve(capacity);
    }

    // Pin a page. On miss, the loader is invoked to materialise the page.
    // The frame's reference counter is bumped (capped) and its pin count
    // incremented; the caller MUST call release() exactly once when done.
    const Page& acquire(const PageId& id, const Loader& loader) {
        std::unique_lock w(rwmu_);
        auto it = lookup_.find(id);
        if (it != lookup_.end()) {
            Frame& f = slots_[it->second];
            heat_up(f);
            ++f.pin_count;
            ++metrics_.hits;
            return f.page;
        }

        ++metrics_.misses;
        std::size_t where;
        if (slots_.size() < capacity_) {
            where = slots_.size();
            slots_.emplace_back();
        } else {
            where = sweep_for_victim();
            ++metrics_.evictions;
            Frame& victim = slots_[where];
            lookup_.erase(victim.id);
        }
        Frame& target = slots_[where];
        target.id        = id;
        target.page      = loader(id);
        target.ref_bits  = 1;          // freshly loaded — start at 1, decays from here
        target.pin_count = 1;
        target.dirty     = false;
        target.live      = true;
        lookup_[id]      = where;
        return target.page;
    }

    // Release one pin. Pass `dirty=true` to mark the frame for write-back.
    void release(const PageId& id, bool dirty = false) {
        std::unique_lock w(rwmu_);
        auto it = lookup_.find(id);
        if (it == lookup_.end()) return;
        Frame& f = slots_[it->second];
        if (f.pin_count > 0) --f.pin_count;
        if (dirty) f.dirty = true;
    }

    // Optional convenience: peek at a frame without bumping its ref bits.
    // Returns nullopt if not cached. Does NOT pin — internal/debug use.
    std::optional<Page> peek(const PageId& id) const {
        std::shared_lock r(rwmu_);
        auto it = lookup_.find(id);
        if (it == lookup_.end()) return std::nullopt;
        return slots_[it->second].page;
    }

    // Write back every dirty frame via the supplied writer, clearing the
    // dirty flag once the writer returns. Walks frames in insertion order.
    std::size_t flush_all(const Writer& writer) {
        std::unique_lock w(rwmu_);
        std::size_t flushed = 0;
        for (auto& f : slots_) {
            if (f.live && f.dirty) {
                writer(f.id, f.page);
                f.dirty = false;
                ++flushed;
            }
        }
        return flushed;
    }

    // Render a snapshot of the pool state in clock order, useful for tracing.
    void render(std::ostream& os, const std::string& label = "") const {
        std::shared_lock r(rwmu_);
        os << "+-- pool";
        if (!label.empty()) os << " (" << label << ")";
        os << ": cap=" << capacity_
           << "  size=" << slots_.size()
           << "  hand=" << hand_
           << "  hits=" << metrics_.hits
           << "  miss=" << metrics_.misses
           << "  evict=" << metrics_.evictions
           << "\n";
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const Frame& f = slots_[i];
            os << "  " << (i == hand_ ? "@" : " ")
               << " slot[" << i << "] "
               << "id=" << f.id
               << "  ref=" << f.ref_bits
               << "  pin=" << f.pin_count
               << (f.dirty ? "  *dirty*" : "")
               << (f.live  ? "" : "  (empty)")
               << "\n";
        }
    }

    Metrics metrics() const {
        std::shared_lock r(rwmu_);
        return metrics_;
    }
    std::size_t size() const {
        std::shared_lock r(rwmu_);
        return slots_.size();
    }
    std::size_t capacity() const noexcept { return capacity_; }

private:
    struct Frame {
        PageId id{};
        Page   page{};
        int    ref_bits  = 0;
        int    pin_count = 0;
        bool   dirty     = false;
        bool   live      = false;
    };

    void heat_up(Frame& f) {
        if (f.ref_bits < kRefBitsCap) ++f.ref_bits;
    }

    // Walk the clock hand forward until we find an unpinned, cold frame.
    // Pinned frames are skipped; warm frames have their ref_bits decayed.
    // Caller already holds rwmu_ in exclusive mode.
    std::size_t sweep_for_victim() {
        const std::size_t n = slots_.size();
        // Hard upper bound on steps so a buggy state can't spin forever.
        const std::size_t guard = static_cast<std::size_t>(kRefBitsCap + 1) * n + 1;

        for (std::size_t step = 0; step < guard; ++step) {
            std::size_t here = hand_;
            advance_hand();

            Frame& f = slots_[here];
            ++metrics_.hand_visits;

            if (f.pin_count > 0) continue;          // skip — somebody still holds it
            if (f.ref_bits   > 0) { --f.ref_bits; continue; }   // second chance
            return here;
        }
        throw std::runtime_error("BufferPool: every frame is pinned, cannot evict");
    }

    void advance_hand() {
        const std::size_t n = slots_.size();
        hand_ = hand_ + 1;
        if (hand_ >= n) { hand_ = 0; ++metrics_.hand_rounds; }
    }

    const std::size_t capacity_;
    std::vector<Frame> slots_;
    std::unordered_map<PageId, std::size_t> lookup_;
    std::size_t hand_ = 0;
    Metrics     metrics_{};
    mutable std::shared_mutex rwmu_;
};

}  // namespace kt
