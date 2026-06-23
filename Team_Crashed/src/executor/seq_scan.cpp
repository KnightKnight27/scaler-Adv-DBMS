// =============================================================================
// src/executor/seq_scan.cpp
// -----------------------------------------------------------------------------
// SeqScanExecutor: walks the heap file row by row, applies a predicate
// (if any), and materialises a Tuple per surviving row.
//
// Row encoding follows the Schema's column layout: for every column we
// reserve `length` bytes (4 for INT/FLOAT/BOOL, declared for VARCHAR),
// producing a fixed-size row image that fits in a single slot.
//
// The predicate / column-lookup / row-decode helpers used to live in an
// anonymous namespace here. They were extracted to
// include/executor/predicate_eval.h so DeleteExecutor can use them too.
// =============================================================================
#include "executor/seq_scan.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "common/record_id.h"
#include "common/status.h"
#include "common/types.h"
#include "executor/executor.h"
#include "executor/predicate_eval.h"
#include "parser/ast.h"
#include "storage/heap_file.h"

namespace minidb::executor {

// All helpers live in src/executor/predicate_eval.cpp. Including this TU
// keeps the existing call sites (`colBytes`, `decodeColumn`, `evalPredicate`)
// link-clean: the header re-exports the symbols into this translation unit.

// ----- SeqScanExecutor -----

SeqScanExecutor::SeqScanExecutor(ExecutorContext* ctx,
                                 std::string table,
                                 std::unique_ptr<parser::Expr> predicate)
    : Executor(ctx), table_(std::move(table)),
      predicate_(std::move(predicate)) {}

// Default the destructor explicitly so the unique_ptr members get the
// generated destructor in this translation unit.
SeqScanExecutor::~SeqScanExecutor() = default;

// Open the table, build a HeapFile, and start a forward scan iterator.
Status SeqScanExecutor::init() {
    info_ = ctx_->cat->getTable(table_);
    if (!info_) return Status::NOT_FOUND;
    file_ = std::make_unique<storage::HeapFile>(ctx_->bp, info_);
    it_   = file_->scan();
    return Status::OK;
}

// Pull the next surviving (predicate-passing) row, decoding it into out.
Status SeqScanExecutor::next(Tuple& out) {
    if (!it_) return Status::DONE;
    RecordId rid;
    std::span<const std::uint8_t> bytes;
    while (it_->next(rid, bytes)) {
        out.values.clear();
        const auto& cols = info_->schema.columns();
        std::size_t off = 0;
        for (const auto& col : cols) {
            std::size_t n = colBytes(col);
            Value v = decodeColumn(col, bytes.data() + off);
            out.values.push_back(v);
            off += n;
        }
        if (!predicate_) return Status::OK;
        if (evalPredicate(*predicate_, out, info_->schema)) return Status::OK;
        // Predicate failed — keep scanning.
    }
    return Status::DONE;
}

// Close the underlying iterator.
Status SeqScanExecutor::close() {
    if (it_) it_->close();
    return Status::OK;
}

} // namespace minidb::executor
