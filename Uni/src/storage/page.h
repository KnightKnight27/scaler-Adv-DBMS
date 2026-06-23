#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using PageId_t = int32_t;
using Lsn_t = uint64_t;
using TxId_t = int32_t;

constexpr int PAGE_SIZE = 4096;
constexpr PageId_t INVALID_PAGE_ID = -1;

struct RID {
    PageId_t page_id = INVALID_PAGE_ID;
    uint16_t slot_id = 0;

    bool operator==(const RID& o) const {
        return page_id == o.page_id && slot_id == o.slot_id;
    }
    bool operator!=(const RID& o) const {
        return !(*this == o);
    }
    bool IsValid() const {
        return page_id != INVALID_PAGE_ID;
    }
};

enum class PageType : uint8_t {
    DATA_PAGE = 0,
    INDEX_PAGE = 1
};

#pragma pack(push, 1)
struct PageHeader {
    Lsn_t page_lsn;
    uint8_t page_type;
    uint16_t slot_count;
    uint16_t free_space_pointer; // Offset from start of page
    PageId_t next_page_id;       // Chained page for heap files
};

struct Slot {
    uint16_t offset;
    uint16_t length; // length == 0 means deleted
};

// For MVCC versioned records
struct MVCCHeader {
    TxId_t xmin;
    TxId_t xmax;
    PageId_t prev_page_id;
    uint16_t prev_slot_id;
};
#pragma pack(pop)

struct Page {
    char data[PAGE_SIZE];

    PageHeader* GetHeader() {
        return reinterpret_cast<PageHeader*>(data);
    }

    const PageHeader* GetHeader() const {
        return reinterpret_cast<const PageHeader*>(data);
    }

    void Init(PageId_t page_id, PageType type) {
        std::memset(data, 0, PAGE_SIZE);
        PageHeader* header = GetHeader();
        header->page_lsn = 0;
        header->page_type = static_cast<uint8_t>(type);
        header->slot_count = 0;
        header->free_space_pointer = PAGE_SIZE;
        header->next_page_id = INVALID_PAGE_ID;
    }

    Slot* GetSlots() {
        return reinterpret_cast<Slot*>(data + sizeof(PageHeader));
    }

    const Slot* GetSlots() const {
        return reinterpret_cast<const Slot*>(data + sizeof(PageHeader));
    }
};
