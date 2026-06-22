#ifndef CLOCK_REPLACER_H
#define CLOCK_REPLACER_H

#include "common/config.h"
#include <vector>
#include <mutex>

namespace minidb {

class ClockReplacer {
public:
    explicit ClockReplacer(size_t num_pages);
    ~ClockReplacer();

    bool Victim(frame_id_t* frame_id);
    void Pin(frame_id_t frame_id);
    void Unpin(frame_id_t frame_id);
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
