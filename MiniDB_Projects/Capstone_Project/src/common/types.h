#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @file types.h
 * @brief Core type definitions and identifiers used across MiniDB.
 */

// =============================================================================
// Page and Record Identifiers
// =============================================================================

/**
 * @brief Unique identifier for a page in a database file.
 * PageID 0 is reserved for file metadata/header information.
 * Data pages containing table slots start indexing from 1.
 */
using PageID = uint32_t;

/**
 * @brief Sentinel value indicating an invalid or unallocated PageID.
 */
constexpr PageID INVALID_PAGE_ID = UINT32_MAX;

/**
 * @brief Index locating a slot inside a slotted page.
 */
using SlotID = uint16_t;

/**
 * @brief Sentinel value indicating an invalid or unallocated SlotID.
 */
constexpr SlotID INVALID_SLOT_ID = UINT16_MAX;

/**
 * @brief Represents a physical pointer to a record's location on disk.
 * Combines the PageID of the page containing the slot, and the SlotID inside it.
 */
struct RecordID {
    PageID page_id = INVALID_PAGE_ID; ///< Page ID where the record resides
    SlotID slot_id = INVALID_SLOT_ID; ///< Slot index of the record on that page

    /**
     * @brief Checks if the record ID is currently pointing to a valid location.
     * @return True if both page_id and slot_id are valid.
     */
    bool isValid() const {
        return (page_id != INVALID_PAGE_ID) && (slot_id != INVALID_SLOT_ID);
    }

    /**
     * @brief Equality comparison operator.
     */
    bool operator==(const RecordID& other) const {
        return (page_id == other.page_id) && (slot_id == other.slot_id);
    }

    /**
     * @brief Inequality comparison operator.
     */
    bool operator!=(const RecordID& other) const {
        return !(*this == other);
    }
};

// =============================================================================
// Transaction Identifiers
// =============================================================================

/**
 * @brief Unique identifier for an active database transaction.
 * Indexed starting from 1. TxID 0 is reserved for non-transactional system operations.
 */
using TxID = uint64_t;

/**
 * @brief Sentinel value indicating an invalid or empty Transaction ID.
 */
constexpr TxID INVALID_TX_ID = 0;

// =============================================================================
// Log Sequence Numbers (WAL)
// =============================================================================

/**
 * @brief Monotonically increasing identifier for Write-Ahead Log records.
 * Crucial for coordinating recovery playbacks and replication offsets.
 */
using LSN = uint64_t;

/**
 * @brief Sentinel value indicating an invalid or unassigned Log Sequence Number.
 */
constexpr LSN INVALID_LSN = 0;
