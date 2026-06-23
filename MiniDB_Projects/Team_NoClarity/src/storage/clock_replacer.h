#ifndef CLOCK_REPLACER_H
#define CLOCK_REPLACER_H

#include "common/config.h"
#include <vector>
#include <mutex>

namespace minidb {

/**
 * Clock / Second-Chance page replacement tracking policy for frame eviction candidate selection.
 */
class ClockReplacer {
public:
    explicit ClockReplacer(size_t num_pages);
    ~ClockReplacer();

    // Selects a frame victim to evict from buffer pool
    bool Victim(frame_id_t* frame_id);
    
    // Removes page frame from replacer consideration (pinned in memory)
    void Pin(frame_id_t frame_id);
    
    // Inserts or keeps page frame in replacer consideration (unpinned)
    void Unpin(frame_id_t frame_id);
    
    // Size of replacer candidates
    size_t Size();

private:
    size_t num_pages_;
    std::vector<bool> in_replacer_;
    std::vector<bool> ref_flags_;
    size_t clock_hand_{0};
    std::mutex latch_;
};

} // namespace minidb

#endif // CLOCK_REPLACER_H
