#pragma once

#include "common/config.h"
#include "common/types.h"
#include <cstring>
#include <vector>

namespace minidb {

class Page {
public:
    Page() { ResetMemory(); }
    ~Page() = default;

    char *GetData() { return data_; }
    const char *GetData() const { return data_; }
    
    page_id_t GetPageId() const { return page_id_; }
    void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    lsn_t GetLSN() const {
        return *reinterpret_cast<const lsn_t *>(data_ + sizeof(page_id_t));
    }
    void SetLSN(lsn_t lsn) {
        *reinterpret_cast<lsn_t *>(data_ + sizeof(page_id_t)) = lsn;
    }

    void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
    }

private:
    char data_[PAGE_SIZE];
    page_id_t page_id_{INVALID_PAGE_ID};
};

// Slotted-Page Layout helper class
class SlottedPage {
public:
    struct Slot {
        uint16_t offset;
        uint16_t length;
    };

    explicit SlottedPage(Page *page) : page_(page) {}

    void Init(page_id_t page_id) {
        page_->SetPageId(page_id);
        page_->SetLSN(INVALID_LSN);
        SetSlotCount(0);
        SetFreeSpacePointer(PAGE_SIZE);
        SetNextPageId(INVALID_PAGE_ID);
    }

    page_id_t GetNextPageId() const {
        return *reinterpret_cast<const page_id_t *>(page_->GetData() + sizeof(page_id_t) + sizeof(lsn_t) + sizeof(uint16_t) + sizeof(uint16_t));
    }

    void SetNextPageId(page_id_t next_page_id) {
        *reinterpret_cast<page_id_t *>(page_->GetData() + sizeof(page_id_t) + sizeof(lsn_t) + sizeof(uint16_t) + sizeof(uint16_t)) = next_page_id;
    }

    page_id_t GetPageId() const { return page_->GetPageId(); }
    lsn_t GetLSN() const { return page_->GetLSN(); }
    void SetLSN(lsn_t lsn) { page_->SetLSN(lsn); }

    uint16_t GetSlotCount() const {
        return *reinterpret_cast<const uint16_t *>(page_->GetData() + sizeof(page_id_t) + sizeof(lsn_t));
    }

    void SetSlotCount(uint16_t count) {
        *reinterpret_cast<uint16_t *>(page_->GetData() + sizeof(page_id_t) + sizeof(lsn_t)) = count;
    }

    uint16_t GetFreeSpacePointer() const {
        return *reinterpret_cast<const uint16_t *>(page_->GetData() + sizeof(page_id_t) + sizeof(lsn_t) + sizeof(uint16_t));
    }

    void SetFreeSpacePointer(uint16_t fsp) {
        *reinterpret_cast<uint16_t *>(page_->GetData() + sizeof(page_id_t) + sizeof(lsn_t) + sizeof(uint16_t)) = fsp;
    }

    Slot GetSlot(slot_id_t slot_id) const {
        if (slot_id < 0 || slot_id >= GetSlotCount()) {
            return {0, 0};
        }
        const char *slot_ptr = page_->GetData() + GetHeaderSize() + slot_id * sizeof(Slot);
        return *reinterpret_cast<const Slot *>(slot_ptr);
    }

    void SetSlot(slot_id_t slot_id, Slot slot) {
        char *slot_ptr = page_->GetData() + GetHeaderSize() + slot_id * sizeof(Slot);
        *reinterpret_cast<Slot *>(slot_ptr) = slot;
    }

    uint16_t GetFreeSpace() const {
        uint16_t slots_end = GetHeaderSize() + GetSlotCount() * sizeof(Slot);
        uint16_t fsp = GetFreeSpacePointer();
        if (fsp < slots_end) return 0;
        return fsp - slots_end;
    }

    bool InsertTuple(const char *data, uint16_t size, RID &rid) {
        uint16_t space_needed = sizeof(Slot) + size;
        if (GetFreeSpace() < space_needed) {
            return false;
        }

        uint16_t fsp = GetFreeSpacePointer() - size;
        std::memcpy(page_->GetData() + fsp, data, size);
        SetFreeSpacePointer(fsp);

        // Find a deleted/empty slot, or append a new one
        slot_id_t slot_id = -1;
        uint16_t slot_count = GetSlotCount();
        for (uint16_t i = 0; i < slot_count; ++i) {
            Slot s = GetSlot(i);
            if (s.length == 0) { // deleted slot
                slot_id = i;
                break;
            }
        }

        if (slot_id == -1) {
            slot_id = slot_count;
            SetSlotCount(slot_count + 1);
        }

        SetSlot(slot_id, {fsp, size});
        rid = {GetPageId(), slot_id};
        return true;
    }

    bool GetTuple(slot_id_t slot_id, char *data, uint16_t &size) const {
        Slot slot = GetSlot(slot_id);
        if (slot.length == 0) {
            return false;
        }
        size = slot.length;
        std::memcpy(data, page_->GetData() + slot.offset, slot.length);
        return true;
    }

    bool UpdateTuple(slot_id_t slot_id, const char *data, uint16_t size) {
        Slot slot = GetSlot(slot_id);
        if (slot.length == 0) {
            return false;
        }

        if (size <= slot.length) {
            // Overwrite in place, keeping the same offset but updating size
            std::memcpy(page_->GetData() + slot.offset, data, size);
            SetSlot(slot_id, {slot.offset, size});
            return true;
        }

        // Needs more space
        if (GetFreeSpace() < size) {
            return false; // not enough space on this page
        }

        uint16_t fsp = GetFreeSpacePointer() - size;
        std::memcpy(page_->GetData() + fsp, data, size);
        SetFreeSpacePointer(fsp);
        SetSlot(slot_id, {fsp, size});
        return true;
    }

    bool RestoreTuple(slot_id_t slot_id, const char *data, uint16_t size) {
        uint16_t slot_count = GetSlotCount();
        if (slot_id >= slot_count) {
            SetSlotCount(slot_id + 1);
            for (uint16_t i = slot_count; i < slot_id; ++i) {
                SetSlot(i, {0, 0});
            }
        }
        uint16_t fsp = GetFreeSpacePointer() - size;
        std::memcpy(page_->GetData() + fsp, data, size);
        SetFreeSpacePointer(fsp);
        SetSlot(slot_id, {fsp, size});
        return true;
    }

    bool DeleteTuple(slot_id_t slot_id) {
        Slot slot = GetSlot(slot_id);
        if (slot.length == 0) {
            return false;
        }
        // Mark as deleted
        SetSlot(slot_id, {0, 0});
        // Note: in a production DB we would compact the page, but for simplicity
        // we can just leave it. Compact is optional.
        return true;
    }

    // Helper to get raw page
    Page *GetPage() { return page_; }

private:
    static constexpr uint16_t GetHeaderSize() {
        return sizeof(page_id_t) + sizeof(lsn_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(page_id_t);
    }

    Page *page_;
};

} // namespace minidb
