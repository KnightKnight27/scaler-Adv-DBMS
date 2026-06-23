#ifndef PAGE_H
#define PAGE_H

#include "common/config.h"
#include <cstring>
#include <shared_mutex>

namespace minidb {

/**
 * Standard database block representation holding active raw data frame and pinning flags.
 */
class Page {
    friend class BufferPoolManager;
public:
    Page() { ResetMemory(); }
    ~Page() = default;

    inline char* GetData() { return data_; }
    inline const char* GetData() const { return data_; }
    inline page_id_t GetPageId() const { return page_id_; }
    inline int GetPinCount() const { return pin_count_; }
    inline bool IsDirty() const { return is_dirty_; }
    inline void SetDirty(bool is_dirty) { is_dirty_ = is_dirty; }
    inline lsn_t GetPageLSN() const { return page_lsn_; }
    inline void SetPageLSN(lsn_t lsn) { page_lsn_ = lsn; }

    inline void RLock() { rw_latch_.lock_shared(); }
    inline void RUnlock() { rw_latch_.unlock_shared(); }
    inline void WLock() { rw_latch_.lock(); }
    inline void WUnlock() { rw_latch_.unlock(); }

    inline void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_ = false;
        page_lsn_ = 0;
    }

private:
    char data_[PAGE_SIZE];
    page_id_t page_id_{INVALID_PAGE_ID};
    int pin_count_{0};
    bool is_dirty_{false};
    lsn_t page_lsn_{0};
    std::shared_mutex rw_latch_;
};

} // namespace minidb

#endif // PAGE_H
