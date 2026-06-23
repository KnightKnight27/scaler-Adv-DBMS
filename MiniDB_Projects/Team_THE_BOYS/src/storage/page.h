#pragma once

#include <cstdint>
#include <vector>

#include "common/types.h"

namespace minidb {

class Page {
public:
    explicit Page(int page_id, const char* data = nullptr);

    int page_id() const { return page_id_; }
    bool dirty() const { return dirty_; }
    void set_dirty(bool dirty) { dirty_ = dirty; }
    int pin_count() const { return pin_count_; }
    void set_pin_count(int count) { pin_count_ = count; }
    uint64_t lsn() const { return lsn_; }
    void set_lsn(uint64_t lsn) { lsn_ = lsn; }

    void Initialize();
    std::optional<int> InsertTuple(const std::vector<uint8_t>& raw);
    std::optional<std::vector<uint8_t>> GetTuple(int slot_id) const;
    bool DeleteTuple(int slot_id);
    std::vector<int> ValidSlots() const;

    const char* Data() const { return data_.data(); }
    char* MutableData() { return data_.data(); }
    void WriteBack(char* dest) const;

    static std::vector<uint8_t> SerializeRow(const Row& row);
    static Row DeserializeRow(const std::vector<uint8_t>& raw);

private:
    int page_id_;
    std::vector<char> data_;
    bool dirty_ = false;
    int pin_count_ = 0;
    uint64_t lsn_ = 0;

    uint16_t NumSlots() const;
    uint16_t FreeSpace() const;
    void SetHeader(uint16_t num_slots, uint16_t free_space);
    int SlotOffset(int slot_id) const;
    std::pair<int, int> ReadSlot(int slot_id) const;
    void WriteSlot(int slot_id, int offset, int length);
};

}  // namespace minidb
