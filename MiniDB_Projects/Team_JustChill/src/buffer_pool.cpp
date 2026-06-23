#include "buffer_pool.h"
#include <iostream>

BufferPool::BufferPool(int pool_size, HeapFile* disk_mgr)
    : capacity(pool_size), disk_manager(disk_mgr), frames(new Page[pool_size]) {
}

BufferPool::~BufferPool() {
    // Write all dirty pages to disk before destroying
    for (auto const& [pid, idx] : page_table) {
        if (frames[idx].is_dirty) {
            disk_manager->writePage(pid, &frames[idx]);
        }
    }
    delete[] frames;
}

Page* BufferPool::getPage(int page_id) {
    std::lock_guard<std::mutex> lock(pool_lock);

    if (page_id < 0) {
        return nullptr;
    }

    // Check if the page is already in the buffer pool
    auto it = page_table.find(page_id);
    if (it != page_table.end()) {
        int idx = it->second;
        // Move page_id to the front of the LRU list
        lru_list.remove(page_id);
        lru_list.push_front(page_id);
        
        // Pin the page
        frames[idx].pin_count++;
        return &frames[idx];
    }

    // Cache miss - find an empty frame
    int free_idx = -1;
    for (int i = 0; i < capacity; ++i) {
        if (frames[i].page_id == -1) {
            free_idx = i;
            break;
        }
    }

    // If no empty frames, perform eviction
    if (free_idx == -1) {
        if (!evictPage()) {
            // Buffer pool is full and all pages are pinned (cannot evict)
            return nullptr;
        }
        
        // Find the newly freed frame
        for (int i = 0; i < capacity; ++i) {
            if (frames[i].page_id == -1) {
                free_idx = i;
                break;
            }
        }
    }

    if (free_idx == -1) {
        return nullptr;
    }

    // Read the page from disk into the free frame
    disk_manager->readPage(page_id, &frames[free_idx]);

    // Track the new page
    frames[free_idx].pin_count = 1;
    page_table[page_id] = free_idx;
    lru_list.push_front(page_id);

    return &frames[free_idx];
}

void BufferPool::unpinPage(int page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(pool_lock);

    auto it = page_table.find(page_id);
    if (it == page_table.end()) {
        return; // Page is not in the buffer pool
    }

    int idx = it->second;
    if (frames[idx].pin_count > 0) {
        frames[idx].pin_count--;
    }

    if (is_dirty) {
        frames[idx].is_dirty = true;
    }
}

bool BufferPool::evictPage() {
    // Scan lru_list from the back (least recently used) to find an unpinned page
    auto it = lru_list.end();
    while (it != lru_list.begin()) {
        --it;
        int pid = *it;
        int idx = page_table[pid];
        if (frames[idx].pin_count == 0) {
            // Write back to disk if dirty
            if (frames[idx].is_dirty) {
                disk_manager->writePage(pid, &frames[idx]);
            }
            
            // Reset the frame
            frames[idx].reset();
            
            // Remove the page from the page table and LRU list
            page_table.erase(pid);
            lru_list.erase(it);
            
            return true;
        }
    }
    return false; // No page can be evicted (all are pinned)
}
