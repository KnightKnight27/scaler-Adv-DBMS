#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "storage/tuple.h"

namespace minidb {

using PageId = int32_t;
constexpr PageId INVALID_PAGE_ID = -1;
constexpr size_t PAGE_SIZE = 4096;

// A Page is a 4KB chunk of memory.
// Since we are using fixed-size tuples, we can use a simple layout.
// The page will have a small header and an array of tuples.
constexpr size_t MAX_TUPLES_PER_PAGE = (PAGE_SIZE - 16) / TUPLE_SIZE; // 16 bytes for header

struct Page {
    PageId page_id_{INVALID_PAGE_ID};
    bool is_dirty_{false};
    int pin_count_{0};

    char data_[PAGE_SIZE]; 

    Page() { ResetMemory(); }

    void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        is_dirty_ = false;
        pin_count_ = 0;
    }
};

constexpr uint32_t HEAP_MAGIC = 0x48454150; // "HEAP"
constexpr uint32_t META_MAGIC = 0x4D455441; // "META"

struct MetaPage {
    uint32_t magic_;
    PageId root_page_id_;
};

struct HeapPage {
    uint32_t magic_;
    int32_t num_tuples_;
    
    void Init() {
        magic_ = HEAP_MAGIC;
        num_tuples_ = 0;
    }
    
    Tuple* GetTuples() {
        return reinterpret_cast<Tuple*>(reinterpret_cast<char*>(this) + sizeof(HeapPage));
    }
    
    bool InsertTuple(const Tuple& tuple) {
        int max_tuples = (PAGE_SIZE - sizeof(HeapPage)) / sizeof(Tuple);
        if (num_tuples_ >= max_tuples) return false;
        GetTuples()[num_tuples_++] = tuple;
        return true;
    }
};

} // namespace minidb
