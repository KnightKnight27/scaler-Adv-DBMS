#pragma once

#include "storage/page.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace minidb {

struct [[gnu::packed]] RecordId {
    page_id_t page_id;
    uint16_t slot_id;
};

struct [[gnu::packed]] BNodeHeader {
    uint8_t is_leaf;
    uint16_t key_count;
    page_id_t next_leaf_page_id;
};

struct [[gnu::packed]] MetaPageData {
    page_id_t root_page_id;
    page_id_t next_page_id;
    uint32_t degree_t;
    uint32_t magic;
};

constexpr std::size_t BNODE_HEADER_SIZE = sizeof(BNodeHeader);
constexpr std::size_t META_PAGE_DATA_SIZE = sizeof(MetaPageData);
constexpr uint32_t BTREE_META_MAGIC = 0x49445842U;  // "BIDX"
constexpr page_id_t BTREE_META_PAGE_ID = 0;

class BNodePage {
public:
    explicit BNodePage(char* data) : data_(data) {}

    BNodeHeader GetHeader() const {
        BNodeHeader header{};
        std::memcpy(&header, data_, BNODE_HEADER_SIZE);
        return header;
    }

    void SetHeader(const BNodeHeader& header) {
        std::memcpy(data_, &header, BNODE_HEADER_SIZE);
    }

    int64_t GetKey(int degree, std::size_t index) const {
        int64_t key = 0;
        std::memcpy(&key, data_ + KeyOffset(degree, index), sizeof(int64_t));
        return key;
    }

    void SetKey(int degree, std::size_t index, int64_t key) {
        std::memcpy(data_ + KeyOffset(degree, index), &key, sizeof(int64_t));
    }

    page_id_t GetChild(int degree, std::size_t index) const {
        page_id_t child = INVALID_PAGE_ID;
        std::memcpy(&child, data_ + ChildOffset(degree, index), sizeof(page_id_t));
        return child;
    }

    void SetChild(int degree, std::size_t index, page_id_t child) {
        std::memcpy(data_ + ChildOffset(degree, index), &child, sizeof(page_id_t));
    }

    RecordId GetRecord(int degree, std::size_t index) const {
        RecordId rid{};
        std::memcpy(&rid, data_ + RecordOffset(degree, index), sizeof(RecordId));
        return rid;
    }

    void SetRecord(int degree, std::size_t index, const RecordId& rid) {
        std::memcpy(data_ + RecordOffset(degree, index), &rid, sizeof(RecordId));
    }

    static std::size_t MaxKeys(int degree) {
        return static_cast<std::size_t>(2 * degree - 1);
    }

    static std::size_t MinKeys(int degree, bool is_root) {
        if (is_root) {
            return 0;
        }
        return static_cast<std::size_t>(degree - 1);
    }

    static std::size_t KeyOffset(int degree, std::size_t index) {
        (void)degree;
        return BNODE_HEADER_SIZE + index * sizeof(int64_t);
    }

    static std::size_t ChildOffset(int degree, std::size_t index) {
        return BNODE_HEADER_SIZE + MaxKeys(degree) * sizeof(int64_t) +
               index * sizeof(page_id_t);
    }

    static std::size_t RecordOffset(int degree, std::size_t index) {
        return BNODE_HEADER_SIZE + MaxKeys(degree) * sizeof(int64_t) +
               index * sizeof(RecordId);
    }

    static std::size_t InternalPayloadSize(int degree) {
        return ChildOffset(degree, MaxKeys(degree) + 1);
    }

    static std::size_t LeafPayloadSize(int degree) {
        return RecordOffset(degree, MaxKeys(degree));
    }

    static bool FitsOnPage(int degree, bool is_leaf) {
        const std::size_t payload =
            is_leaf ? LeafPayloadSize(degree) : InternalPayloadSize(degree);
        return payload <= PAGE_SIZE;
    }

    static int MaxSupportedDegree() {
        int degree = 2;
        while (FitsOnPage(degree, false) && FitsOnPage(degree, true)) {
            ++degree;
        }
        return degree - 1;
    }

private:
    char* data_;
};

class MetaPage {
public:
    explicit MetaPage(char* data) : data_(data) {}

    MetaPageData Read() const {
        MetaPageData meta{};
        std::memcpy(&meta, data_, META_PAGE_DATA_SIZE);
        return meta;
    }

    void Write(const MetaPageData& meta) {
        std::memcpy(data_, &meta, META_PAGE_DATA_SIZE);
    }

private:
    char* data_;
};

}  // namespace minidb
