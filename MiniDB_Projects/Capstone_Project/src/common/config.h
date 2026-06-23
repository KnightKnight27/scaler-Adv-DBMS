#pragma once

#include <cstddef>

/**
 * @file config.h
 * @brief Global configuration constants for the MiniDB database engine.
 */

// =============================================================================
// Storage Subsystem Configurations
// =============================================================================

/**
 * @brief Size of each database page in bytes.
 * Matches SQLite's default page size of 4096 bytes (4 KB).
 * All physical disk pages and in-memory buffer pool frames conform to this size.
 */
constexpr size_t PAGE_SIZE = 4096;

/**
 * @brief Number of page frames managed in the buffer pool.
 * Limits the maximum number of pages kept concurrently cached in memory.
 */
constexpr size_t BUFFER_POOL_SIZE = 64;

/**
 * @brief Maximum byte size allowed for a single record's payload.
 * Used to simplify slotted-page fragmentation and layout constraints.
 */
constexpr size_t MAX_RECORD_SIZE = 256;

/**
 * @brief Maximum length for database table names (including null terminator).
 */
constexpr size_t MAX_TABLE_NAME_LEN = 32;

// =============================================================================
// Index Subsystem Configurations
// =============================================================================

/**
 * @brief Order of the primary key B+ Tree index.
 * Defines that internal nodes store up to (BTREE_ORDER - 1) keys and BTREE_ORDER children,
 * and leaf nodes store up to (BTREE_ORDER - 1) key-value entries.
 * BTREE_ORDER = 4 keeps splitting logic and visual demonstrations straightforward.
 */
constexpr int BTREE_ORDER = 4;

// =============================================================================
// WAL & Recovery Configurations
// =============================================================================

/**
 * @brief Maximum character buffer size for the value field inside a WAL record.
 */
constexpr size_t WAL_MAX_VALUE_SIZE = 256;

// =============================================================================
// Replication Subsystem Configurations
// =============================================================================

/**
 * @brief Polling frequency (in milliseconds) for the replica process
 * to check for newly appended replication log records.
 */
constexpr int REPLICA_POLL_MS = 100;

// =============================================================================
// Cost Optimizer Model Configurations
// =============================================================================

/**
 * @brief Abstract cost weight assigned to performing a single page disk read/write.
 */
constexpr double IO_PAGE_COST = 1.0;
