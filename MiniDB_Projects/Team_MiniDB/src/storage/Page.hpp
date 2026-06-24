#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

using namespace std;

#include "common/Config.hpp"

namespace minidb {

enum class PageType : uint32_t { HEAP = 0, INDEX = 1, FREE = 2 };

class Page {
public:
    Page();
    void init(uint32_t page_id, PageType type);

    uint32_t getPageId() const;
    PageType getPageType() const;
    uint32_t getRecordCount() const;
    uint32_t getFreeSpaceOffset() const;

    void setRecordCount(uint32_t count);
    void setFreeSpaceOffset(uint32_t offset);

    const char* getData() const;
    char* getData();

    bool insertRecord(const vector<char>& bytes);
    bool getRecord(uint32_t slot_index, vector<char>& out) const;
    bool deleteRecord(uint32_t slot_index);
    bool isSlotDeleted(uint32_t slot_index) const;

private:
    char data_[PAGE_SIZE];
    static constexpr uint32_t HEADER_SIZE = 16;
    static constexpr uint32_t SLOT_ENTRY_SIZE = 4;

    uint32_t slotDirectoryOffset() const;
    uint32_t maxSlots() const;
};

}  // namespace minidb
