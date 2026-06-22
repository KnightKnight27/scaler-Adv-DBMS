#include "storage/clock_replacer.h"

namespace minidb {

ClockReplacer::ClockReplacer(size_t num_pages)
    : num_pages_(num_pages), in_replacer_(num_pages, false), ref_flags_(num_pages, false) {}

ClockReplacer::~ClockReplacer() = default;

bool ClockReplacer::Victim(frame_id_t* frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (num_pages_ == 0) return false;

    size_t scan_count = 0;
    while (scan_count < 2 * num_pages_) {
        if (in_replacer_[clock_hand_]) {
            if (ref_flags_[clock_hand_]) {
                ref_flags_[clock_hand_] = false;
            } else {
                in_replacer_[clock_hand_] = false;
                *frame_id = static_cast<frame_id_t>(clock_hand_);
                clock_hand_ = (clock_hand_ + 1) % num_pages_;
                return true;
            }
        }
        clock_hand_ = (clock_hand_ + 1) % num_pages_;
        scan_count++;
    }
    return false;
}

void ClockReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (frame_id >= 0 && static_cast<size_t>(frame_id) < num_pages_) {
        in_replacer_[frame_id] = false;
    }
}

void ClockReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (frame_id >= 0 && static_cast<size_t>(frame_id) < num_pages_) {
        in_replacer_[frame_id] = true;
        ref_flags_[frame_id] = true;
    }
}

size_t ClockReplacer::Size() {
    std::lock_guard<std::mutex> lock(latch_);
    size_t size = 0;
    for (bool val : in_replacer_) {
        if (val) size++;
    }
    return size;
}

} // namespace minidb
