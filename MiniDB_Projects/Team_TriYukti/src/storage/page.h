#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <string>

namespace minidb {

constexpr int PAGE_SIZE = 4096;
constexpr int INVALID_PAGE_ID = -1;

typedef int32_t page_id_t;
typedef uint16_t slot_id_t;

struct RecordId {
    page_id_t page_id;
    slot_id_t slot_id;
    
    bool operator==(const RecordId &other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
};

class Tuple {
public:
    Tuple() = default;
    Tuple(const std::vector<uint8_t> &data) : data_(data) {}
    Tuple(std::vector<uint8_t> &&data) : data_(std::move(data)) {}
    
    int32_t GetCreatedBy() const {
        if (data_.size() < 8) return -1;
        int32_t val;
        memcpy(&val, data_.data(), 4);
        return val;
    }
    
    int32_t GetDeletedBy() const {
        if (data_.size() < 8) return -1;
        int32_t val;
        memcpy(&val, data_.data() + 4, 4);
        return val;
    }
    
    void SetCreatedBy(int32_t val) {
        if (data_.size() >= 8) memcpy(data_.data(), &val, 4);
    }
    
    void SetDeletedBy(int32_t val) {
        if (data_.size() >= 8) memcpy(data_.data() + 4, &val, 4);
    }
    
    std::vector<uint8_t> data_;
    RecordId rid_;
};

class Page {
public:
    Page();
    ~Page() = default;

    void Init(page_id_t page_id);
    
    bool InsertTuple(const Tuple &tuple, RecordId *rid);
    bool DeleteTuple(slot_id_t slot_id);
    bool UpdateTuple(slot_id_t slot_id, const Tuple &tuple);
    bool GetTuple(slot_id_t slot_id, Tuple *tuple) const;

    page_id_t GetPageId() const;
    void SetPageId(page_id_t page_id);
    
    page_id_t GetNextPageId() const;
    void SetNextPageId(page_id_t next_page_id);
    
    int32_t GetLSN() const;
    void SetLSN(int32_t lsn);

    char* GetData();
    const char* GetData() const;
    
    uint16_t GetTupleCount() const;

private:
    char data_[PAGE_SIZE];

    // Helper functions for slotted page
    void SetTupleCount(uint16_t tuple_count);

    uint16_t GetFreeSpacePointer() const;
    void SetFreeSpacePointer(uint16_t free_space_pointer);

    uint16_t GetTupleOffset(slot_id_t slot_id) const;
    void SetTupleOffset(slot_id_t slot_id, uint16_t offset);

    uint16_t GetTupleLength(slot_id_t slot_id) const;
    void SetTupleLength(slot_id_t slot_id, uint16_t length);
    
    uint16_t GetFreeSpaceRemaining() const;
};

} // namespace minidb
