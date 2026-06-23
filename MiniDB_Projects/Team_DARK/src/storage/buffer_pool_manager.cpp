#include "storage/buffer_pool_manager.h"

#include <algorithm>
#include <stdexcept>

namespace minidb {

BufferPoolManager::BufferPoolManager(DiskManager* disk_manager, std::size_t pool_size)
    : disk_manager_(disk_manager),
      frames_(pool_size),
      pool_size_(pool_size),
      clock_hand_(0) {
    if (disk_manager_ == nullptr) {
        throw std::invalid_argument("disk_manager must not be null");
    }
    if (pool_size_ == 0) {
        throw std::invalid_argument("pool_size must be greater than zero");
    }

    for (auto& frame : frames_) {
        frame.data = DiskManager::AllocatePageBuffer();
    }
}

BufferPoolManager::~BufferPoolManager() {
    std::lock_guard<std::mutex> lock(latch_);
    FlushAllPagesUnlocked();
    for (auto& frame : frames_) {
        DiskManager::FreePageBuffer(frame.data);
        frame.data = nullptr;
    }
}

char* BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    const auto existing = page_table_.find(page_id);
    if (existing != page_table_.end()) {
        Frame& frame = frames_[existing->second];
        frame.usage_count =
            std::min(static_cast<uint8_t>(frame.usage_count + 1), kMaxUsageCount);
        ++frame.pin_count;
        return frame.data;
    }

    const int victim = FindVictimFrame();
    if (victim < 0) {
        throw std::runtime_error("all buffer pool frames are pinned");
    }

    EvictFrame(static_cast<std::size_t>(victim));

    Frame& frame = frames_[static_cast<std::size_t>(victim)];
    disk_manager_->ReadPage(page_id, frame.data);
    frame.page_id = page_id;
    frame.usage_count = 1;
    frame.pin_count = 1;
    frame.is_dirty = false;
    page_table_[page_id] = static_cast<std::size_t>(victim);

    return frame.data;
}

void BufferPoolManager::UnpinPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        throw std::invalid_argument("page not found in buffer pool");
    }

    Frame& frame = frames_[it->second];
    if (frame.pin_count <= 0) {
        throw std::runtime_error("pin_count underflow");
    }
    --frame.pin_count;
}

void BufferPoolManager::MarkDirty(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        throw std::invalid_argument("page not found in buffer pool");
    }

    frames_[it->second].is_dirty = true;
}

void BufferPoolManager::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;
    }

    Frame& frame = frames_[it->second];
    if (!frame.is_dirty) {
        return;
    }

    disk_manager_->WritePage(page_id, frame.data);
    frame.is_dirty = false;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    FlushAllPagesUnlocked();
}

void BufferPoolManager::FlushAllPagesUnlocked() {
    for (const auto& entry : page_table_) {
        Frame& frame = frames_[entry.second];
        if (!frame.is_dirty) {
            continue;
        }
        disk_manager_->WritePage(entry.first, frame.data);
        frame.is_dirty = false;
    }
}

int BufferPoolManager::FindVictimFrame() {
    std::size_t checked = 0;
    const std::size_t max_checks =
        pool_size_ * (static_cast<std::size_t>(kMaxUsageCount) + 2);

    while (checked < max_checks) {
        Frame& frame = frames_[clock_hand_];

        if (frame.pin_count == 0) {
            if (frame.usage_count == 0) {
                const int victim = static_cast<int>(clock_hand_);
                clock_hand_ = (clock_hand_ + 1) % pool_size_;
                return victim;
            }
            --frame.usage_count;
        }

        clock_hand_ = (clock_hand_ + 1) % pool_size_;
        ++checked;
    }

    return -1;
}

void BufferPoolManager::EvictFrame(int frame_index) {
    Frame& frame = frames_[static_cast<std::size_t>(frame_index)];
    if (frame.page_id == INVALID_PAGE_ID) {
        return;
    }

    if (frame.is_dirty) {
        disk_manager_->WritePage(frame.page_id, frame.data);
        frame.is_dirty = false;
    }

    page_table_.erase(frame.page_id);
    frame.page_id = INVALID_PAGE_ID;
    frame.usage_count = 0;
    frame.pin_count = 0;
}

}  // namespace minidb
