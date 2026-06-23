#pragma once
#include "page.h"
#include "heap_file.h"
#include <unordered_map>
#include <cstdint>

static constexpr size_t POOL_MAX = 64;

struct PageKey {
    uintptr_t file_addr;
    uint32_t  page_id;
    bool operator==(const PageKey& o) const {
        return file_addr == o.file_addr && page_id == o.page_id;
    }
};

struct PageKeyHash {
    size_t operator()(const PageKey& k) const {
        return std::hash<uintptr_t>{}(k.file_addr) ^ (std::hash<uint32_t>{}(k.page_id) << 32);
    }
};

struct CachedPage {
    HeapFile* file;
    Page      page;
};

class BufferPool {
public:
    Page& get_page(HeapFile& file, uint32_t page_id);
    void  mark_dirty(HeapFile& file, uint32_t page_id);
    void  flush_all(HeapFile& file);
    void  clear() { cache_.clear(); }

private:
    std::unordered_map<PageKey, CachedPage, PageKeyHash> cache_;
    void evict_one();
};
