#include "BufferPool.h"

#include <stdexcept>

BufferPool::BufferPool(DiskManager* diskManager, int capacity)
    : diskManager_(diskManager), capacity_(capacity)
{
    if (diskManager == nullptr) {
        throw std::invalid_argument("DiskManager must not be null");
    }
    if (capacity <= 0) {
        throw std::invalid_argument("Capacity must be > 0");
    }
}

BufferPool::BufferPool(DiskManager* diskManager)
    : BufferPool(diskManager, DEFAULT_CAPACITY)
{
}

BufferPool::~BufferPool() {
    flushAllPages();

    // Explicitly delete all heap-allocated Page objects
    for (auto& pair : pageTable_) {
        delete pair.second;
    }
    pageTable_.clear();
    lruList_.clear();
    lruMap_.clear();
    pinCounts_.clear();
}

Page* BufferPool::fetchPage(int pageId) {
    // Check if page is already in the cache
    auto it = pageTable_.find(pageId);

    if (it != pageTable_.end()) {
        // Cache hit — promote in LRU
        promoteLRU(pageId);
        pinCounts_[pageId]++;
        return it->second;
    }

    // Cache miss — may need to evict
    if (static_cast<int>(pageTable_.size()) >= capacity_) {
        evictPage();
    }

    // Read page from disk into a new heap-allocated Page
    uint8_t buf[Page::PAGE_SIZE];
    diskManager_->readPage(pageId, buf);

    Page* page = new Page(pageId, buf);
    pageTable_[pageId] = page;

    // Add to front of LRU list (most recently used)
    lruList_.push_front(pageId);
    lruMap_[pageId] = lruList_.begin();

    // Pin the page
    pinCounts_[pageId] = 1;

    return page;
}

void BufferPool::unpinPage(int pageId, bool isDirty) {
    auto it = pageTable_.find(pageId);
    if (it == pageTable_.end()) {
        return;
    }

    if (isDirty) {
        it->second->markDirty();
    }

    int currentPins = pinCounts_[pageId];
    if (currentPins > 0) {
        pinCounts_[pageId] = currentPins - 1;
    }
}

void BufferPool::evictPage() {
    // Scan from back (least recently used) to find an unpinned page
    for (auto rit = lruList_.rbegin(); rit != lruList_.rend(); ++rit) {
        int candidateId = *rit;

        if (pinCounts_[candidateId] == 0) {
            // Found an evictable page
            Page* evictPage = pageTable_[candidateId];

            // Write to disk if dirty
            if (evictPage->isDirty()) {
                diskManager_->writePage(evictPage->getPageId(),
                                        evictPage->getData());
            }

            // Remove from all data structures
            pageTable_.erase(candidateId);
            pinCounts_.erase(candidateId);

            // Remove from LRU list using the stored iterator
            auto listIt = lruMap_[candidateId];
            lruList_.erase(listIt);
            lruMap_.erase(candidateId);

            // Free the Page memory
            delete evictPage;
            return;
        }
    }

    throw std::runtime_error(
        "Buffer pool is full and all pages are pinned!");
}

void BufferPool::promoteLRU(int pageId) {
    auto mapIt = lruMap_.find(pageId);
    if (mapIt == lruMap_.end()) return;

    // Move to front of LRU list
    lruList_.erase(mapIt->second);
    lruList_.push_front(pageId);
    lruMap_[pageId] = lruList_.begin();
}

void BufferPool::flushAllPages() {
    for (auto& pair : pageTable_) {
        Page* page = pair.second;
        if (page->isDirty()) {
            diskManager_->writePage(page->getPageId(), page->getData());
            page->clearDirty();
        }
    }
}
