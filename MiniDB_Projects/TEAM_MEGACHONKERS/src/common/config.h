#pragma once

#include <cstdint>
#include <cstddef>

namespace minidb {

constexpr size_t MEMTABLE_MAX_SIZE = 4 * 1024 * 1024; // 4 MB flush threshold
constexpr size_t SSTABLE_BLOCK_SIZE = 4096;           // 4 KB disk blocks for reading

using table_oid_t = uint32_t;
using txn_id_t = uint32_t;
using lsn_t = uint64_t; // Log Sequence Number for the WAL

constexpr table_oid_t INVALID_TABLE_OID = 0;
constexpr txn_id_t INVALID_TXN_ID = 0;

} // namespace minidb