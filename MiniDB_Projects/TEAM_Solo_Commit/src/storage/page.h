// MiniDB - a 4KB slotted page, the unit of storage and buffering.
//
// Layout (classic slotted page):
//   [ num_slots:u16 | free_ptr:u16 | slot[0] | slot[1] | ... ->     <free>     <- ... tuple data ]
//   slot directory grows forward from the header; tuple payloads grow backward from the end.
//   Each slot is (offset:u16, length:u16). A length of 0 marks a deleted (tombstoned) slot;
//   the slot itself is kept so existing RIDs and index entries stay valid.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace minidb {

static constexpr int PAGE_SIZE = 4096;
static constexpr int INVALID_PAGE_ID = -1;

class Page {
public:
    Page() { Reset(); }

    char* data() { return data_; }
    const char* data() const { return data_; }

    void Reset() {
        std::memset(data_, 0, PAGE_SIZE);
        SetNumSlots(0);
        SetFreePtr(PAGE_SIZE);
    }

    uint16_t NumSlots() const { return ReadU16(0); }
    uint16_t FreePtr() const  { return ReadU16(2); }

    // Bytes available for one more (slot + payload of size `len`).
    int FreeSpace() const {
        int slot_end = HEADER_SIZE + NumSlots() * SLOT_SIZE;
        return FreePtr() - slot_end;
    }

    // Insert a record. Returns the new slot number, or -1 if it does not fit.
    int Insert(const std::string& rec) {
        int need = static_cast<int>(rec.size()) + SLOT_SIZE;
        if (need > FreeSpace()) return -1;
        uint16_t new_free = static_cast<uint16_t>(FreePtr() - rec.size());
        std::memcpy(data_ + new_free, rec.data(), rec.size());
        uint16_t slot = NumSlots();
        WriteSlot(slot, new_free, static_cast<uint16_t>(rec.size()));
        SetFreePtr(new_free);
        SetNumSlots(slot + 1);
        return slot;
    }

    bool Read(int slot, std::string* out) const {
        if (slot < 0 || slot >= NumSlots()) return false;
        uint16_t off = SlotOffset(slot), len = SlotLength(slot);
        if (len == 0) return false;  // tombstoned
        out->assign(data_ + off, len);
        return true;
    }

    // Tombstone a slot. Space is not reclaimed (kept simple); RIDs stay stable.
    bool Delete(int slot) {
        if (slot < 0 || slot >= NumSlots()) return false;
        if (SlotLength(slot) == 0) return false;
        WriteSlot(slot, 0, 0);
        return true;
    }

    bool IsLive(int slot) const {
        return slot >= 0 && slot < NumSlots() && SlotLength(slot) != 0;
    }

private:
    static constexpr int HEADER_SIZE = 4;  // num_slots(2) + free_ptr(2)
    static constexpr int SLOT_SIZE = 4;    // offset(2) + length(2)

    uint16_t ReadU16(int off) const {
        return static_cast<uint8_t>(data_[off]) | (static_cast<uint8_t>(data_[off + 1]) << 8);
    }
    void WriteU16(int off, uint16_t v) {
        data_[off] = static_cast<char>(v & 0xFF);
        data_[off + 1] = static_cast<char>((v >> 8) & 0xFF);
    }
    void SetNumSlots(uint16_t n) { WriteU16(0, n); }
    void SetFreePtr(uint16_t p)  { WriteU16(2, p); }

    int SlotPos(int slot) const { return HEADER_SIZE + slot * SLOT_SIZE; }
    uint16_t SlotOffset(int slot) const { return ReadU16(SlotPos(slot)); }
    uint16_t SlotLength(int slot) const { return ReadU16(SlotPos(slot) + 2); }
    void WriteSlot(int slot, uint16_t off, uint16_t len) {
        WriteU16(SlotPos(slot), off);
        WriteU16(SlotPos(slot) + 2, len);
    }

    char data_[PAGE_SIZE];
};

}  // namespace minidb
