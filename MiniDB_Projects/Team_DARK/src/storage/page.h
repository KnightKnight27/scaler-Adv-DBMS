#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace minidb {

constexpr std::size_t PAGE_SIZE = 4096;
constexpr std::size_t PAGE_HEADER_SIZE = 24;
constexpr std::size_t SLOT_ENTRY_SIZE = 4;
constexpr std::size_t ROW_VERSION_HEADER_SIZE = 24;
constexpr uint64_t INVALID_VERSION_TID = 0;

using page_id_t = int32_t;
constexpr page_id_t INVALID_PAGE_ID = -1;

struct PageHeader {
    uint32_t checksum;
    uint32_t slot_count;
    uint32_t free_space_pointer;
    uint64_t lsn;
    uint32_t reserved;
};

struct SlotEntry {
    uint16_t offset;
    uint16_t length;
};

struct RowVersionHeader {
    uint64_t xmin;
    uint64_t xmax;
    uint64_t prev_version_tid;
};

class Page {
public:
    explicit Page(char* data) : data_(data) {}

    PageHeader GetHeader() const {
        PageHeader header{};
        std::memcpy(&header.checksum, data_, sizeof(uint32_t));
        std::memcpy(&header.slot_count, data_ + 4, sizeof(uint32_t));
        std::memcpy(&header.free_space_pointer, data_ + 8, sizeof(uint32_t));
        std::memcpy(&header.lsn, data_ + 12, sizeof(uint64_t));
        std::memcpy(&header.reserved, data_ + 20, sizeof(uint32_t));
        return header;
    }

    void SetHeader(const PageHeader& header) {
        std::memcpy(data_, &header.checksum, sizeof(uint32_t));
        std::memcpy(data_ + 4, &header.slot_count, sizeof(uint32_t));
        std::memcpy(data_ + 8, &header.free_space_pointer, sizeof(uint32_t));
        std::memcpy(data_ + 12, &header.lsn, sizeof(uint64_t));
        std::memcpy(data_ + 20, &header.reserved, sizeof(uint32_t));
    }

    SlotEntry GetSlot(std::size_t index) const {
        SlotEntry slot{};
        const std::size_t offset = PAGE_HEADER_SIZE + index * SLOT_ENTRY_SIZE;
        std::memcpy(&slot.offset, data_ + offset, sizeof(uint16_t));
        std::memcpy(&slot.length, data_ + offset + 2, sizeof(uint16_t));
        return slot;
    }

    void SetSlot(std::size_t index, const SlotEntry& slot) {
        const std::size_t offset = PAGE_HEADER_SIZE + index * SLOT_ENTRY_SIZE;
        std::memcpy(data_ + offset, &slot.offset, sizeof(uint16_t));
        std::memcpy(data_ + offset + 2, &slot.length, sizeof(uint16_t));
    }

    char* Data() { return data_; }
    const char* Data() const { return data_; }

    static PageHeader MakeDefaultHeader() {
        PageHeader header{};
        header.checksum = 0;
        header.slot_count = 0;
        header.free_space_pointer = PAGE_SIZE;
        header.lsn = 0;
        header.reserved = 0;
        return header;
    }

    static void InitPage(char* data) {
        std::memset(data, 0, PAGE_SIZE);
        Page page(data);
        page.SetHeader(MakeDefaultHeader());
    }

    RowVersionHeader GetRowVersionHeader(std::size_t row_offset) const {
        RowVersionHeader header{};
        std::memcpy(&header.xmin, data_ + row_offset, sizeof(uint64_t));
        std::memcpy(&header.xmax, data_ + row_offset + 8, sizeof(uint64_t));
        std::memcpy(&header.prev_version_tid, data_ + row_offset + 16, sizeof(uint64_t));
        return header;
    }

    void SetRowVersionHeader(std::size_t row_offset, const RowVersionHeader& header) {
        std::memcpy(data_ + row_offset, &header.xmin, sizeof(uint64_t));
        std::memcpy(data_ + row_offset + 8, &header.xmax, sizeof(uint64_t));
        std::memcpy(data_ + row_offset + 16, &header.prev_version_tid, sizeof(uint64_t));
    }

    static RowVersionHeader MakeDefaultRowVersionHeader(uint64_t xmin) {
        RowVersionHeader header{};
        header.xmin = xmin;
        header.xmax = INVALID_VERSION_TID;
        header.prev_version_tid = INVALID_VERSION_TID;
        return header;
    }

private:
    char* data_;
};

}  // namespace minidb
