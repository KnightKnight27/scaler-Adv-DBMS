#pragma once
#include "page.h"
#include "heap_file.h"
#include <unordered_map>
#include <list>
#include <mutex>

class BufferPool {
private:
    int capacity;
    HeapFile* disk_manager;
    
    Page* frames; // Array of physical pages in memory
    
    std::list<int> lru_list; // Tracks least recently used pages
    std::unordered_map<int, int> page_table; // Maps page_id -> index in 'frames' array
    
    std::mutex pool_lock;

    // Helper to evict an unpinned page
    bool evictPage(); 

public:
    BufferPool(int pool_size, HeapFile* disk_manager);
    ~BufferPool();

    Page* getPage(int page_id);
    void unpinPage(int page_id, bool is_dirty);
};
