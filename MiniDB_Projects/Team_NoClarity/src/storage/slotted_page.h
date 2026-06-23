#ifndef SLOTTED_PAGE_H
#define SLOTTED_PAGE_H

#include "common/config.h"
#include "common/rid.h"
#include <string>

namespace minidb {

/**
 * Utility functions mapping variable length tuples inside 4KB block bytes sequentially.
 */
class SlottedPage {
public:
    struct Slot {
        uint16_t offset;
        uint16_t length;
    };

    static constexpr uint16_t TOMBSTONE = 0xFFFF;

    // Helper utilities to parse slotted page headers in-place over data pointer
    static inline uint16_t GetSlotCount(const char* page_data) {
        return *reinterpret_cast<const uint16_t*>(page_data);
    }

    static inline void SetSlotCount(char* page_data, uint16_t slot_count) {
        *reinterpret_cast<uint16_t*>(page_data) = slot_count;
    }

    static inline uint16_t GetFreeSpacePointer(const char* page_data) {
        return *reinterpret_cast<const uint16_t*>(page_data + 2);
    }

    static inline void SetFreeSpacePointer(char* page_data, uint16_t fsp) {
        *reinterpret_cast<uint16_t*>(page_data + 2) = fsp;
    }

    static inline Slot* GetSlotArray(char* page_data) {
        return reinterpret_cast<Slot*>(page_data + 4);
    }

    static inline const Slot* GetSlotArray(const char* page_data) {
        return reinterpret_cast<const Slot*>(page_data + 4);
    }

    // Initialize an empty slotted page
    static void Init(char* page_data);

    // Appends variable length serialized record to data frames
    static bool InsertTuple(char* page_data, const std::string& tuple, RID* rid, page_id_t page_id);
    
    // Marks target record slot offset index with TOMBSTONE marker
    static bool DeleteTuple(char* page_data, uint32_t slot_index);
    
    // Deserializes tuple content matching specific index key
    static bool GetTuple(const char* page_data, uint32_t slot_index, std::string& tuple);
    
    // Reclaims fragmented deleted tuple spaces in-place
    static void CompactPage(char* page_data);
};

} // namespace minidb

#endif // SLOTTED_PAGE_H
