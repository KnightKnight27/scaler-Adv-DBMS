#pragma once

// ============================================================
//  SQL Query Executor
//  Provides SELECT / INSERT / UPDATE / DELETE on top of:
//    - NSMPage, DSMPage, PAXPage
//    - VersionChainIndex  (MVCC version resolution)
//    - TransactionManager (begin / commit / abort)
// ============================================================

#include "mvcc_types.h"
#include "version_chain.h"
#include "transaction_manager.h"
#include "page_layouts.h"

#include <functional>
#include <vector>
#include <memory>
#include <string>
#include <optional>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

namespace mvcc {

// ────────────────────────────────────────────────────────────
//  Predicate — a column-based filter used in WHERE clauses
// ────────────────────────────────────────────────────────────
struct Predicate {
    ColumnID col;
    enum class Op { EQ, NEQ, LT, LE, GT, GE } op;
    Value    rhs;

    bool eval(const Value& lhs) const {
        if (lhs.isNull || rhs.isNull) return false;
        // For simplicity we compare INT32 / INT64 / VARCHAR only
        switch (lhs.type) {
            case DataType::INT32: {
                int32_t l = lhs.num.i32, r = rhs.num.i32;
                switch (op) {
                    case Op::EQ:  return l == r;
                    case Op::NEQ: return l != r;
                    case Op::LT:  return l <  r;
                    case Op::LE:  return l <= r;
                    case Op::GT:  return l >  r;
                    case Op::GE:  return l >= r;
                }
                return false;
            }
            case DataType::INT64: {
                int64_t l = lhs.num.i64, r = rhs.num.i64;
                switch (op) {
                    case Op::EQ:  return l == r;
                    case Op::NEQ: return l != r;
                    case Op::LT:  return l <  r;
                    case Op::LE:  return l <= r;
                    case Op::GT:  return l >  r;
                    case Op::GE:  return l >= r;
                }
                return false;
            }
            case DataType::VARCHAR: {
                switch (op) {
                    case Op::EQ:  return lhs.str == rhs.str;
                    case Op::NEQ: return lhs.str != rhs.str;
                    default:      return false;
                }
            }
            default: return false;
        }
    }

    bool evalTuple(const Tuple& t) const {
        if (col >= t.size()) return false;
        return eval(t[col]);
    }

    static Predicate eq (ColumnID c, Value v){ return {c, Op::EQ,  v}; }
    static Predicate neq(ColumnID c, Value v){ return {c, Op::NEQ, v}; }
    static Predicate lt (ColumnID c, Value v){ return {c, Op::LT,  v}; }
    static Predicate gt (ColumnID c, Value v){ return {c, Op::GT,  v}; }
    static Predicate ge (ColumnID c, Value v){ return {c, Op::GE,  v}; }
};

// ────────────────────────────────────────────────────────────
//  QueryResult
// ────────────────────────────────────────────────────────────
struct QueryResult {
    bool                        success  = true;
    std::string                 errorMsg;
    std::vector<Tuple>          rows;
    std::vector<std::string>    colNames;
    size_t                      rowsAffected = 0;
    double                      execTimeMicros = 0.0;
    PageLayout                  layout;

    void print(std::ostream& os = std::cout) const {
        if (!success) {
            os << "ERROR: " << errorMsg << "\n";
            return;
        }
        // header
        os << "Layout: " << pageLayoutName(layout) << "\n";
        os << "Rows returned: " << rows.size()
           << "  |  Rows affected: " << rowsAffected << "\n";
        if (!colNames.empty()) {
            for (auto& n : colNames) os << std::setw(14) << n;
            os << "\n" << std::string(14 * colNames.size(), '-') << "\n";
        }
        for (auto& row : rows) {
            for (auto& v : row) os << std::setw(14) << v.toString();
            os << "\n";
        }
        os << "\n";
    }
};

// ============================================================
//  SQLExecutor<Page>  — template over a Page layout class
// ============================================================
template<typename PageT>
class SQLExecutor {
public:
    SQLExecutor(PageLayout layout, const TableSchema& schema,
                TransactionManager& txnMgr, VersionChainIndex& vci)
        : layout_(layout), schema_(schema), txnMgr_(txnMgr), vci_(vci),
          nextPageID_(0) {
        addPage();  // create the first page
    }

    // ── INSERT ──────────────────────────────────────────────
    QueryResult execInsert(std::shared_ptr<Transaction>& txn,
                           const Tuple& row) {
        QueryResult qr;
        qr.layout = layout_;
        if (!txn->isActive()) { qr.success = false; qr.errorMsg = "Txn not active"; return qr; }

        // Derive logical key from primary key column
        uint64_t key = primaryKey(row);

        // Check for duplicate (SSI check)
        auto chain = vci_.get(key);
        if (chain) {
            auto head = chain->head();
            if (head && head->status == VersionStatus::COMMITTED &&
                head->endTS == INF_TS) {
                qr.success = false;
                qr.errorMsg = "PRIMARY KEY violation on key " + std::to_string(key);
                return qr;
            }
        }

        // Write to current page
        RID rid{ currentPage_->id(), 0 };
        rid.slot = currentPage_->insert(row, txn->id, txn->beginTS);

        // Register in version chain
        auto newVer = std::make_shared<VersionRecord>(txn->id, txn->beginTS, row, rid);
        auto ch     = vci_.getOrCreate(key);
        ch->append(newVer);

        txn->writeSet.push_back({ key, newVer, nullptr });
        qr.rowsAffected = 1;
        return qr;
    }

    // ── SELECT ──────────────────────────────────────────────
    // Uses VersionChainIndex as the authoritative source:
    // for each logical key walk the version chain to return
    // the single latest visible version (avoids stale duplicates
    // left behind by UPDATE/DELETE append-only writes).
    QueryResult execSelect(std::shared_ptr<Transaction>& txn,
                           const std::vector<ColumnID>& cols,
                           const std::vector<Predicate>& preds = {}) {
        QueryResult qr;
        qr.layout = layout_;
        qr.colNames = buildColNames(cols);

        Timestamp readTS = snapshotTS(txn);

        // Collect unique logical keys visible on physical pages.
        std::unordered_set<uint64_t> seenKeys;
        for (auto& page : pages_) {
            auto rows = scanPage(*page, readTS);
            for (auto& [sid, tuple] : rows)
                seenKeys.insert(primaryKey(tuple));
        }

        // Resolve each key through its version chain (authoritative).
        for (uint64_t key : seenKeys) {
            auto chain = vci_.get(key);
            if (!chain) continue;
            auto resolved = chain->readVersion(readTS);
            if (!resolved) continue;          // deleted or not visible
            if (!applyPreds(*resolved, preds)) continue;

            Tuple projected;
            for (ColumnID c : cols)
                if (c < resolved->size()) projected.push_back((*resolved)[c]);
            qr.rows.push_back(std::move(projected));

            if (txn->isolationLevel == IsolationLevel::SERIALIZABLE)
                txn->readSet.insert(key);
        }
        return qr;
    }

    // ── UPDATE ──────────────────────────────────────────────
    QueryResult execUpdate(std::shared_ptr<Transaction>& txn,
                           const std::vector<Predicate>& preds,
                           const std::vector<std::pair<ColumnID,Value>>& sets) {
        QueryResult qr;
        qr.layout = layout_;
        Timestamp readTS = snapshotTS(txn);

        for (auto& page : pages_) {
            auto rows = scanPage(*page, readTS);
            for (auto& [sid, tuple] : rows) {
                if (!applyPreds(tuple, preds)) continue;

                uint64_t key = primaryKey(tuple);
                auto chain   = vci_.get(key);
                if (!chain) continue;
                auto oldHead = chain->head();
                if (!oldHead) continue;

                // Write-write conflict check: someone else updated after our begin?
                if (oldHead->creatorTxn != txn->id &&
                    oldHead->beginTS > txn->beginTS) {
                    txnMgr_.abort(txn, vci_);
                    throw TxnConflictError("Write conflict on key " + std::to_string(key));
                }

                // Build new tuple
                Tuple newTuple = tuple;
                for (auto& [c, v] : sets)
                    if (c < newTuple.size()) newTuple[c] = v;

                // Append new version
                RID rid{ currentPage_->id(), 0 };
                rid.slot = currentPage_->insert(newTuple, txn->id, txn->beginTS);
                auto newVer = std::make_shared<VersionRecord>(
                    txn->id, txn->beginTS, newTuple, rid);
                chain->append(newVer);

                txn->writeSet.push_back({ key, newVer, oldHead });
                ++qr.rowsAffected;
            }
        }
        return qr;
    }

    // ── DELETE ──────────────────────────────────────────────
    QueryResult execDelete(std::shared_ptr<Transaction>& txn,
                           const std::vector<Predicate>& preds) {
        QueryResult qr;
        qr.layout = layout_;
        Timestamp readTS = snapshotTS(txn);

        for (auto& page : pages_) {
            auto rows = scanPage(*page, readTS);
            for (auto& [sid, tuple] : rows) {
                if (!applyPreds(tuple, preds)) continue;

                uint64_t key = primaryKey(tuple);
                auto chain = vci_.get(key);
                if (!chain) continue;
                auto oldHead = chain->head();
                if (!oldHead) continue;

                // Tombstone version (empty tuple)
                Tuple tombstone;
                RID rid{ currentPage_->id(), 0 };
                auto delVer = std::make_shared<VersionRecord>(
                    txn->id, txn->beginTS, tombstone, rid);
                delVer->status = VersionStatus::DELETED;
                chain->append(delVer);

                txn->writeSet.push_back({ key, delVer, oldHead });
                ++qr.rowsAffected;
            }
        }
        return qr;
    }

    // ── AGGREGATE SUM (demonstrating column push-down) ──────
    // Resolves via version chains so DELETE/UPDATE are honoured.
    QueryResult execAggSum(std::shared_ptr<Transaction>& txn,
                           ColumnID aggCol,
                           const std::vector<Predicate>& preds = {}) {
        QueryResult qr;
        qr.layout = layout_;
        Timestamp readTS = snapshotTS(txn);
        int64_t total = 0;

        // Collect unique keys from physical pages, then resolve via chain.
        std::unordered_set<uint64_t> seenKeys;
        for (auto& page : pages_) {
            auto rows = scanPage(*page, readTS);
            for (auto& [sid, tuple] : rows)
                seenKeys.insert(primaryKey(tuple));
        }

        for (uint64_t key : seenKeys) {
            auto chain = vci_.get(key);
            if (!chain) continue;
            auto resolved = chain->readVersion(readTS);
            if (!resolved) continue;
            if (!applyPreds(*resolved, preds)) continue;
            if (aggCol >= resolved->size()) continue;
            auto& v = (*resolved)[aggCol];
            if (!v.isNull) {
                if (v.type == DataType::INT64) total += v.num.i64;
                if (v.type == DataType::INT32) total += v.num.i32;
            }
        }
        Tuple sumRow{ Value::makeInt64(total) };
        qr.rows.push_back(sumRow);
        qr.colNames.push_back("SUM(" + schema_.columns[aggCol].name + ")");
        return qr;
    }

    PageLayout layout() const { return layout_; }

    void dumpPages(std::ostream& os = std::cout) const {
        for (auto& p : pages_) p->dump(os);
    }

private:
    // Snapshot timestamp: READ COMMITTED sees latest commit;
    //                     SNAPSHOT / SERIALIZABLE sees beginTS
    Timestamp snapshotTS(const std::shared_ptr<Transaction>& txn) const {
        if (txn->isolationLevel == IsolationLevel::READ_COMMITTED)
            return txnMgr_.currentTS();
        return txn->beginTS;
    }

    void addPage() {
        pages_.push_back(std::make_unique<PageT>(nextPageID_++, schema_));
        currentPage_ = pages_.back().get();
    }

    std::vector<std::pair<SlotID, Tuple>> scanPage(const PageT& page,
                                                    Timestamp ts) const {
        return page.scan(ts);
    }

    bool applyPreds(const Tuple& t, const std::vector<Predicate>& preds) const {
        for (auto& p : preds)
            if (!p.evalTuple(t)) return false;
        return true;
    }

    uint64_t primaryKey(const Tuple& t) const {
        ColumnID pkc = schema_.primaryKeyCol;
        if (pkc >= t.size()) return 0;
        auto& v = t[pkc];
        if (v.type == DataType::INT64) return static_cast<uint64_t>(v.num.i64);
        if (v.type == DataType::INT32) return static_cast<uint64_t>(v.num.i32);
        // Hash varchar
        return std::hash<std::string>{}(v.str);
    }

    std::vector<std::string> buildColNames(const std::vector<ColumnID>& cols) const {
        std::vector<std::string> names;
        for (ColumnID c : cols)
            if (c < schema_.columns.size())
                names.push_back(schema_.columns[c].name);
        return names;
    }

    PageLayout         layout_;
    const TableSchema& schema_;
    TransactionManager& txnMgr_;
    VersionChainIndex&  vci_;
    PageID              nextPageID_;
    std::vector<std::unique_ptr<PageT>> pages_;
    PageT*              currentPage_ = nullptr;
};

// Convenience type aliases
using NSMExecutor = SQLExecutor<NSMPage>;
using DSMExecutor = SQLExecutor<DSMPage>;
using PAXExecutor = SQLExecutor<PAXPage>;

} // namespace mvcc
