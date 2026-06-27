// =============================================================================
// include/executor/mvcc.h
// -----------------------------------------------------------------------------
// MVCC version metadata for the executor path.
//
// Each row image stored by InsertExecutor is the schema's fixed-size column
// layout (see Schema::rowSize()) followed by an 8-byte MVCC trailer:
//
//     [ schema columns ... ][ created_txn : u32 ][ deleted_txn : u32 ]
//
//   created_txn = the txn id that inserted this row version (0 == "no
//                 trailer", i.e. a legacy row written without a transaction
//                 context — always treated as visible/committed).
//   deleted_txn = the txn id that deleted this version, or 0 while live.
//
// The trailer lives AFTER the schema columns so column offsets (and hence
// B+ tree key encoding, which reads from columnOffset()) are unchanged —
// appending the trailer cannot shift any indexed column. HeapFile slots
// store the record length, so a scan can tell a trailed row (length ==
// rowSize + 8) from a legacy one (length == rowSize).
//
// The trailer is written only when a statement runs inside a transaction
// (ExecutorContext::currentTxnId valid). The visibility test below is a
// no-op when there is no reader transaction, so the legacy autocommit path
// is unaffected.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include "common/types.h"
#include "transaction/transaction.h"
#include "transaction/transaction_manager.h"

namespace minidb::executor {

// 8 bytes: two 32-bit txn ids. (Txn ids are monotonic from 1; 32 bits is
// far beyond any capstone workload.)
constexpr std::size_t kMvccTrailerSize = 8;

// True iff the row image carries an MVCC trailer (i.e. was written inside
// a transaction). `schemaRowSize` is Schema::rowSize() for the table.
inline bool hasMvccTrailer(std::size_t schemaRowSize,
                           std::span<const std::uint8_t> bytes) {
    return bytes.size() >= schemaRowSize + kMvccTrailerSize;
}

// Read (created, deleted) from the trailer. Caller MUST ensure
// hasMvccTrailer() is true.
inline std::pair<TransactionId, TransactionId>
readMvccTrailer(std::span<const std::uint8_t> bytes, std::size_t schemaRowSize) {
    std::uint32_t created = 0, deleted = 0;
    const std::uint8_t* base = bytes.data() + schemaRowSize;
    std::memcpy(&created, base,                       sizeof(created));
    std::memcpy(&deleted, base + sizeof(created),     sizeof(deleted));
    return { static_cast<TransactionId>(created),
             static_cast<TransactionId>(deleted) };
}

// Append an MVCC trailer to a serialised row image.
inline void appendMvccTrailer(std::vector<std::uint8_t>& bytes,
                              TransactionId created, TransactionId deleted) {
    const std::size_t start = bytes.size();
    bytes.resize(start + kMvccTrailerSize, 0);
    std::uint32_t c = static_cast<std::uint32_t>(created);
    std::uint32_t d = static_cast<std::uint32_t>(deleted);
    std::memcpy(bytes.data() + start,                  &c, sizeof(c));
    std::memcpy(bytes.data() + start + sizeof(c),      &d, sizeof(d));
}

// Should a row (created by `created`, deleted by `deleted`) be visible to
// `reader`? With no reader transaction (autocommit / legacy), every row is
// visible. Otherwise delegate to TransactionManager::isVisible, which
// implements snapshot isolation against the reader's snapshot.
inline bool visibleTo(transaction::TransactionManager* txnMgr,
                      const transaction::Transaction* reader,
                      TransactionId created, TransactionId deleted) {
    if (txnMgr == nullptr || reader == nullptr) return true;
    return txnMgr->isVisible(created, deleted, *reader);
}

} // namespace minidb::executor