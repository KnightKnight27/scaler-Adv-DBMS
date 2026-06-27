// =============================================================================
// src/executor/delete_executor.cpp
// -----------------------------------------------------------------------------
// DeleteExecutor: drives a SeqScan to find rows matching the WHERE
// predicate, deletes each from the heap file, removes the corresponding
// entry from every index, and appends a WAL DELETE record.
//
// The child scan materialises a Tuple per surviving row. From the Tuple
// we can decode the schema column positions but not the RecordId, so we
// walk the heap file ourselves with `evalPredicate` to map each row back
// to a RecordId (page,slot) and then call `HeapFile::deleteRecord(rid)`.
//
// For v1 we don't have a (table,column) -> index map; we simply clear
// every index's entry under the same key we used when inserting.
// =============================================================================
#include "executor/delete_executor.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "common/record_id.h"
#include "common/status.h"
#include "common/types.h"
#include "executor/executor.h"
#include "executor/predicate_eval.h"
#include "executor/seq_scan.h"
#include "index/bplus_tree.h"
#include "index/index_manager.h"
#include "parser/ast.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "storage/page.h"

namespace minidb::executor {

namespace {

// Encode a B+ tree key from a Value. Mirrors the encoder used by the
// other executors (InsertExecutor, IndexScanExecutor) so we can clear
// the matching index entry.
std::string encodeKey(const Value& v) {
    char buf[16];
    switch (v.tag) {
        case Value::Tag::INT: {
            int32_t x = v.i;
            std::memcpy(buf, &x, sizeof(x));
            return std::string(buf, sizeof(x));
        }
        case Value::Tag::FLOAT: {
            float x = v.f;
            std::memcpy(buf, &x, sizeof(x));
            return std::string(buf, sizeof(x));
        }
        case Value::Tag::BOOL: {
            int32_t x = v.b ? 1 : 0;
            std::memcpy(buf, &x, sizeof(x));
            return std::string(buf, sizeof(x));
        }
        case Value::Tag::STRING:
            return v.s;
        case Value::Tag::NULL_:
            return std::string();
    }
    return std::string();
}

// Pull the predicate's reference to the column that drives an index
// lookup. We use the first column's value (matching the InsertExecutor
// simplification), so this returns its serialised key bytes.
std::string encodeKeyForIndex(const Tuple& row, const catalog::Schema& schema) {
    if (schema.numColumns() == 0) return std::string();
    return encodeKey(row.values.front());
}

} // namespace

// ----- DeleteExecutor -----

DeleteExecutor::DeleteExecutor(ExecutorContext* ctx,
                               std::unique_ptr<parser::DeleteStmt> stmt)
    : Executor(ctx), stmt_(std::move(stmt)) {}

// Out-of-line destructor for the unique_ptr members.
DeleteExecutor::~DeleteExecutor() = default;

// Build a SeqScan over the target table with the WHERE clause as the
// filter predicate. We keep a clone of the WHERE clause in `where_` so
// we can re-apply it while walking the heap file directly.
Status DeleteExecutor::init() {
    if (!stmt_) return Status::INVALID_ARGUMENT;
    if (stmt_->where) where_ = cloneExpr(*stmt_->where);
    child_ = std::make_unique<SeqScanExecutor>(
        ctx_, stmt_->table, std::move(stmt_->where));
    return child_->init();
}

// Walk every row that the scan emits, delete it from the heap file by
// walking the page chain ourselves, and remove it from each index.
//
// The previous version matched by `v0.toString() == t.values[0].toString()`
// which collides on duplicate first-column values. We instead decode each
// row from the heap file and re-apply the same WHERE predicate — the
// SeqScan already used it to filter, but we need it here so the delete
// is keyed off the predicate's columns, not an arbitrary string.
Status DeleteExecutor::next(Tuple& out) {
    (void)out;
    if (!child_) return Status::DONE;

    const catalog::TableInfo* info = ctx_->cat->getTable(stmt_->table);
    if (!info) return Status::NOT_FOUND;
    if (info->firstPageId == INVALID_PAGE_ID) return Status::DONE;

    storage::HeapFile file(ctx_->bp, info);

    // Drain the scan and translate every emitted Tuple into a delete.
    Tuple t;
    while (child_->next(t) == Status::OK) {
        // Walk the heap-file chain, predicate-match each row, and
        // tombstone the slot that matches. We use the *table's*
        // predicate semantics (i.e. the row as stored, not just the
        // first column) so multiple matching rows are all removed.
        PageId pid = info->firstPageId;
        while (pid != INVALID_PAGE_ID) {
            storage::Page* page = nullptr;
            Status s = ctx_->bp->fetchPage(pid, page);
            if (s != Status::OK || page == nullptr) {
                if (page) (void)ctx_->bp->unpinPage(pid, false);
                break;
            }

            const std::uint16_t n = page->slotCount();
            // Slots to delete, paired with the row image captured while the
            // page is still pinned. The before-image is what the WAL DELETE
            // record carries (and what undo re-inserts on recovery).
            std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> toDelete;
            for (std::uint16_t sIdx = 0; sIdx < n; ++sIdx) {
                auto bytes = page->getRecord(sIdx);
                if (bytes.empty()) continue;
                Tuple candidate;
                decodeRow(bytes, info->schema, candidate);
                // Same predicate SeqScan used; if it's missing (no WHERE
                // clause) the child emitted every row, so delete them all.
                if (where_) {
                    if (!evalPredicate(*where_, candidate, info->schema)) continue;
                }
                toDelete.emplace_back(sIdx,
                    std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
            }

            const PageId next = page->nextPage();
            (void)ctx_->bp->unpinPage(pid, false);

            // Now actually delete. We unpin first because deleteRecord
            // will fetch (and pin) the page itself.
            for (auto& [sIdx, beforeImage] : toDelete) {
                RecordId rid = makeRid(pid, sIdx);

                // Concurrency control on the victim row, when running inside
                // a transaction. MVCC: record the write so a concurrent writer
                // of the same rid is caught as a write-write conflict at
                // commit. 2PL: take an X lock; if that would deadlock, skip
                // this row (the txn will abort on the commit conflict check).
                if (ctx_->txn != nullptr && ctx_->currentTxnId != INVALID_TXN_ID) {
                    ctx_->txn->recordWrite(ctx_->currentTxnId, rid);
                    if (ctx_->isoMode == IsoMode::TWO_PL) {
                        Status ls = ctx_->txn->lockManager().acquireExclusive(
                            ctx_->currentTxnId, rid);
                        if (ls == Status::DEADLOCK) continue;
                    }
                }

                Status dStatus = file.deleteRecord(rid);
                bool deleted = (dStatus == Status::OK);
                // Keep the per-table row count in sync for the optimizer.
                if (deleted && ctx_->cat != nullptr) {
                    auto cur = ctx_->cat->cardinality(info->name);
                    if (cur > 0) ctx_->cat->setCardinality(info->name, cur - 1);
                }

                // Append a WAL DELETE record carrying the before-image. The
                // QueryEngine flushes the WAL at statement commit, before any
                // dirty page reaches disk, so write-ahead ordering holds.
                if (ctx_->wal != nullptr &&
                    ctx_->currentTxnId != INVALID_TXN_ID) {
                    recovery::LogRecord rec;
                    rec.kind    = recovery::LogKind::DELETE;
                    rec.txnId   = ctx_->currentTxnId;
                    rec.prevLSN = ctx_->lastLsn;
                    rec.rid     = rid;
                    rec.beforeImage = std::move(beforeImage);
                    // afterImage left empty; the encoder falls back to
                    // beforeImage for DELETE, and undo re-inserts that row.
                    try { ctx_->lastLsn = ctx_->wal->append(rec); }
                    catch (...) { /* best-effort WAL; never crash a delete */ }
                }

                // Remove from every index. v1: encode the first column's
                // value the same way the inserter did.
                std::string key = encodeKeyForIndex(t, info->schema);
                auto names = ctx_->idx->list();
                for (const auto& nm : names) {
                    index::BPlusTree* tree = ctx_->idx->open(nm);
                    if (!tree) continue;
                    (void)tree->remove(key);
                }
            }

            if (next == INVALID_PAGE_ID) break;
            pid = next;
        }
    }
    return Status::DONE;
}

// Close the underlying scan.
Status DeleteExecutor::close() {
    if (child_) return child_->close();
    return Status::OK;
}

} // namespace minidb::executor