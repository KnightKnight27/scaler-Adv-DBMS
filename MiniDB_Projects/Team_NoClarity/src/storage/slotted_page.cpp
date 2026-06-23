#include "storage/slotted_page.h"
#include <cstring>
#include <vector>
#include <iostream>

namespace minidb {

void SlottedPage::Init(char* page_data) {
    SetSlotCount(page_data, 0);
    SetFreeSpacePointer(page_data, PAGE_SIZE);
}

bool SlottedPage::InsertTuple(char* page_data, const std::string& tuple, RID* rid, page_id_t page_id) {
    uint16_t slot_count = GetSlotCount(page_data);
    uint16_t fsp = GetFreeSpacePointer(page_data);
    uint16_t tuple_size = static_cast<uint16_t>(tuple.size());

    // Try to find an existing tombstoned slot to reuse
    int reuse_slot_idx = -1;
    Slot* slots = GetSlotArray(page_data);
    for (uint16_t i = 0; i < slot_count; ++i) {
        if (slots[i].offset == TOMBSTONE) {
            reuse_slot_idx = i;
            break;
        }
    }

    uint16_t needed_space = tuple_size;
    if (reuse_slot_idx == -1) {
        needed_space += sizeof(Slot);
    }

    uint16_t current_free_space = fsp - (4 + slot_count * sizeof(Slot));
    if (current_free_space < needed_space) {
        // Run compaction to reclaim fragmented deleted slots space
        CompactPage(page_data);
        
        // Reload values post-compaction
        slot_count = GetSlotCount(page_data);
        fsp = GetFreeSpacePointer(page_data);
        slots = GetSlotArray(page_data);
        current_free_space = fsp - (4 + slot_count * sizeof(Slot));
        
        if (current_free_space < needed_space) {
            return false; // Still insufficient space
        }
    }

    uint16_t insert_slot_idx;
    if (reuse_slot_idx != -1) {
        insert_slot_idx = static_cast<uint16_t>(reuse_slot_idx);
    } else {
        insert_slot_idx = slot_count;
        SetSlotCount(page_data, slot_count + 1);
    }

    uint16_t new_fsp = fsp - tuple_size;
    std::memcpy(page_data + new_fsp, tuple.data(), tuple_size);
    SetFreeSpacePointer(page_data, new_fsp);

    // Refresh slots pointer and insert metadata
    slots = GetSlotArray(page_data);
    slots[insert_slot_idx].offset = new_fsp;
    slots[insert_slot_idx].length = tuple_size;

    rid->Set(page_id, insert_slot_idx);
    return true;
}

bool SlottedPage::DeleteTuple(char* page_data, uint32_t slot_index) {
    uint16_t slot_count = GetSlotCount(page_data);
    if (slot_index >= slot_count) {
        return false;
    }
    Slot* slots = GetSlotArray(page_data);
    if (slots[slot_index].offset == TOMBSTONE) {
        return false; // Already deleted
    }

    slots[slot_index].offset = TOMBSTONE;
    slots[slot_index].length = 0;
    return true;
}

bool SlottedPage::GetTuple(const char* page_data, uint32_t slot_index, std::string& tuple) {
    uint16_t slot_count = GetSlotCount(page_data);
    if (slot_index >= slot_count) {
        return false;
    }
    const Slot* slots = GetSlotArray(page_data);
    if (slots[slot_index].offset == TOMBSTONE) {
        return false;
    }

    tuple.assign(page_data + slots[slot_index].offset, slots[slot_index].length);
    return true;
}

void SlottedPage::CompactPage(char* page_data) {
    uint16_t slot_count = GetSlotCount(page_data);
    Slot* slots = GetSlotArray(page_data);

    struct ActiveTuple {
        uint16_t slot_index;
        std::string data;
    };
    std::vector<ActiveTuple> active_tuples;

    // Collect all non-tombstoned active records
    for (uint16_t i = 0; i < slot_count; ++i) {
        if (slots[i].offset != TOMBSTONE) {
            active_tuples.push_back({i, std::string(page_data + slots[i].offset, slots[i].length)});
        }
    }

    // Set FSP back to the end of the page
    uint16_t fsp = PAGE_SIZE;

    // Pack data back-to-back at the end of the page
    for (const auto& tuple : active_tuples) {
        fsp -= tuple.data.size();
        std::memcpy(page_data + fsp, tuple.data.data(), tuple.data.size());
        slots[tuple.slot_index].offset = fsp;
        slots[tuple.slot_index].length = static_cast<uint16_t>(tuple.data.size());
    }

    SetFreeSpacePointer(page_data, fsp);
}

} // namespace minidb
