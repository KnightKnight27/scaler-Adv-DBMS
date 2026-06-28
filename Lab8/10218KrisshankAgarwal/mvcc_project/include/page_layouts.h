#pragma once

// ============================================================
//  Page Layout Implementations
//  NSM  — N-ary Storage Model (Row-Oriented)
//  DSM  — Decomposition Storage Model (Column-Oriented)
//  PAX  — Partition Attributes Across (Hybrid)
//
//  Each layout class models how tuples/columns are physically
//  arranged within a 4 KB page.  For MVCC each row is written
//  by stamping begin/end timestamps directly in the slot header.
// ============================================================

#include "mvcc_types.h"
#include "version_chain.h"
#include <vector>
#include <optional>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <iomanip>

namespace mvcc {

// ────────────────────────────────────────────────────────────
//  Slot Header — embedded in every physical slot (all layouts)
// ────────────────────────────────────────────────────────────
struct SlotHeader {
    TxnID     creatorTxn;
    Timestamp beginTS;
    Timestamp endTS;
    bool      deleted;
};

// ============================================================
//  NSM Page  (Row Store)
//  Layout:
//    [ SlotDirectory | → free space ← | tuples ]
//  Each tuple:  [SlotHeader][col0][col1]...[colN]
//  Best for:    OLTP — fetching full rows by primary key
// ============================================================
class NSMPage {
public:
    struct Slot {
        SlotHeader hdr;
        Tuple      tuple;
    };

    explicit NSMPage(PageID pid, const TableSchema& schema)
        : pageID_(pid), schema_(schema) {}

    // Insert a new row; returns slot index
    SlotID insert(const Tuple& t, TxnID txn, Timestamp bts) {
        validateTuple(t);
        SlotID sid = static_cast<SlotID>(slots_.size());
        Slot s;
        s.hdr = { txn, bts, INF_TS, false };
        s.tuple = t;
        slots_.push_back(std::move(s));
        return sid;
    }

    // Read a row visible at readTS (returns nullopt if not found / not visible)
    std::optional<Tuple> read(SlotID sid, Timestamp readTS) const {
        if (sid >= slots_.size()) return std::nullopt;
        const auto& s = slots_[sid];
        if (s.hdr.deleted) return std::nullopt;
        if (!isVisible(s.hdr, readTS)) return std::nullopt;
        return s.tuple;
    }

    // Update: logically seal old slot, return new SlotID
    SlotID update(SlotID oldSid, const Tuple& newData, TxnID txn,
                  Timestamp bts, Timestamp sealTS) {
        if (oldSid >= slots_.size())
            throw std::out_of_range("NSM: slot out of range");
        slots_[oldSid].hdr.endTS = sealTS;   // seal old version
        return insert(newData, txn, bts);
    }

    // Logical delete
    void remove(SlotID sid, Timestamp sealTS) {
        if (sid >= slots_.size()) return;
        slots_[sid].hdr.endTS  = sealTS;
        slots_[sid].hdr.deleted = true;
    }

    // Full scan — returns all visible tuples at readTS
    std::vector<std::pair<SlotID, Tuple>> scan(Timestamp readTS) const {
        std::vector<std::pair<SlotID, Tuple>> result;
        for (SlotID i = 0; i < slots_.size(); ++i) {
            auto t = read(i, readTS);
            if (t) result.emplace_back(i, *t);
        }
        return result;
    }

    // Project specific columns (for select-list pushdown)
    std::vector<std::pair<SlotID, Tuple>> project(
            const std::vector<ColumnID>& cols, Timestamp readTS) const {
        std::vector<std::pair<SlotID, Tuple>> result;
        for (SlotID i = 0; i < slots_.size(); ++i) {
            auto t = read(i, readTS);
            if (!t) continue;
            Tuple projected;
            for (ColumnID c : cols) {
                if (c < t->size()) projected.push_back((*t)[c]);
            }
            result.emplace_back(i, std::move(projected));
        }
        return result;
    }

    PageID id()      const { return pageID_; }
    size_t numSlots() const { return slots_.size(); }

    // Estimate page fill ratio (mock: each slot ≈ 64 bytes)
    double fillRatio() const {
        size_t used = slots_.size() * 64;
        return std::min(1.0, static_cast<double>(used) / PAGE_SIZE);
    }

    void dump(std::ostream& os = std::cout) const {
        os << "NSM Page " << pageID_ << " [" << slots_.size() << " slots, "
           << std::fixed << std::setprecision(1) << fillRatio()*100 << "% full]\n";
        for (SlotID i = 0; i < slots_.size(); ++i) {
            const auto& s = slots_[i];
            os << "  Slot " << i << " txn=" << s.hdr.creatorTxn
               << " ts=[" << s.hdr.beginTS << ",";
            if (s.hdr.endTS == INF_TS) os << "INF"; else os << s.hdr.endTS;
            os << ") " << (s.hdr.deleted ? "[DEL]" : "     ") << " | ";
            for (size_t c = 0; c < s.tuple.size(); ++c) {
                os << schema_.columns[c].name << "=" << s.tuple[c].toString();
                if (c+1 < s.tuple.size()) os << ", ";
            }
            os << "\n";
        }
    }

private:
    bool isVisible(const SlotHeader& h, Timestamp ts) const {
        return h.beginTS <= ts && ts < h.endTS;
    }
    void validateTuple(const Tuple& t) const {
        if (t.size() != schema_.columns.size())
            throw std::invalid_argument("NSM: tuple width != schema width");
    }

    PageID            pageID_;
    const TableSchema& schema_;
    std::vector<Slot> slots_;
};

// ============================================================
//  DSM Page  (Column Store)
//  Layout:
//    For each column: [SlotHeader+value]×N (one column array per column)
//  Best for:   OLAP — full column scans, aggregations, projections
// ============================================================
class DSMPage {
public:
    struct ColSlot {
        SlotHeader hdr;
        Value      val;
    };

    explicit DSMPage(PageID pid, const TableSchema& schema)
        : pageID_(pid), schema_(schema) {
        columns_.resize(schema.columns.size());
    }

    // Insert: all column arrays grow by one cell
    SlotID insert(const Tuple& t, TxnID txn, Timestamp bts) {
        if (t.size() != columns_.size())
            throw std::invalid_argument("DSM: tuple width != schema width");
        SlotID sid = (columns_.empty()) ? 0
                     : static_cast<SlotID>(columns_[0].size());
        SlotHeader hdr{ txn, bts, INF_TS, false };
        for (size_t c = 0; c < columns_.size(); ++c)
            columns_[c].push_back({ hdr, t[c] });
        return sid;
    }

    // Read a single column value (column projection — very fast in DSM)
    std::optional<Value> readColumn(SlotID sid, ColumnID col,
                                    Timestamp readTS) const {
        if (col >= columns_.size() || sid >= columns_[col].size())
            return std::nullopt;
        const auto& cs = columns_[col][sid];
        if (cs.hdr.deleted || !isVisible(cs.hdr, readTS))
            return std::nullopt;
        return cs.val;
    }

    // Reconstruct full row (expensive in DSM — column fetch + stitch)
    std::optional<Tuple> readRow(SlotID sid, Timestamp readTS) const {
        if (columns_.empty() || sid >= columns_[0].size())
            return std::nullopt;
        Tuple t;
        for (ColumnID c = 0; c < columns_.size(); ++c) {
            auto v = readColumn(sid, c, readTS);
            if (!v) return std::nullopt;
            t.push_back(*v);
        }
        return t;
    }

    // Column scan — returns all visible values for column col
    std::vector<Value> scanColumn(ColumnID col, Timestamp readTS) const {
        std::vector<Value> result;
        if (col >= columns_.size()) return result;
        for (SlotID i = 0; i < columns_[col].size(); ++i) {
            auto v = readColumn(i, col, readTS);
            if (v) result.push_back(*v);
        }
        return result;
    }

    // Full row scan — same interface as NSMPage / PAXPage (used by SQLExecutor)
    std::vector<std::pair<SlotID, Tuple>> scan(Timestamp readTS) const {
        std::vector<std::pair<SlotID, Tuple>> result;
        size_t n = numRows();
        for (SlotID i = 0; i < static_cast<SlotID>(n); ++i) {
            auto t = readRow(i, readTS);
            if (t) result.emplace_back(i, *t);
        }
        return result;
    }

    // Aggregate: SUM over INT64 column
    int64_t aggregateSum(ColumnID col, Timestamp readTS) const {
        auto vals = scanColumn(col, readTS);
        int64_t s = 0;
        for (auto& v : vals)
            if (!v.isNull && v.type == DataType::INT64) s += v.num.i64;
            else if (!v.isNull && v.type == DataType::INT32) s += v.num.i32;
        return s;
    }

    // Update: seal old slot in every column, then insert new version
    SlotID update(SlotID oldSid, const Tuple& newData, TxnID txn,
                  Timestamp bts, Timestamp sealTS) {
        for (auto& col : columns_)
            if (oldSid < col.size()) {
                col[oldSid].hdr.endTS = sealTS;
                col[oldSid].hdr.deleted = true;
            }
        return insert(newData, txn, bts);
    }

    PageID id() const { return pageID_; }
    size_t numRows() const {
        return columns_.empty() ? 0 : columns_[0].size();
    }

    double fillRatio() const {
        size_t used = numRows() * columns_.size() * 16;
        return std::min(1.0, static_cast<double>(used) / PAGE_SIZE);
    }

    void dump(std::ostream& os = std::cout) const {
        os << "DSM Page " << pageID_ << " [" << numRows() << " rows × "
           << columns_.size() << " cols, "
           << std::fixed << std::setprecision(1) << fillRatio()*100 << "% full]\n";
        for (ColumnID c = 0; c < columns_.size(); ++c) {
            os << "  Col[" << schema_.columns[c].name << "]: ";
            for (SlotID i = 0; i < columns_[c].size(); ++i) {
                const auto& cs = columns_[c][i];
                os << "[" << i << ":" << cs.val.toString()
                   << " ts=" << cs.hdr.beginTS << "-";
                if (cs.hdr.endTS == INF_TS) os << "INF"; else os << cs.hdr.endTS;
                os << "] ";
            }
            os << "\n";
        }
    }

private:
    bool isVisible(const SlotHeader& h, Timestamp ts) const {
        return h.beginTS <= ts && ts < h.endTS;
    }

    PageID             pageID_;
    const TableSchema& schema_;
    // columns_[colIndex][slotIndex]
    std::vector<std::vector<ColSlot>> columns_;
};

// ============================================================
//  PAX Page  (Partition Attributes Across — Hybrid)
//  Layout (within one 4KB page):
//    [ Mini-row-directory | col-0 minipage | col-1 minipage | ... ]
//  Each minipage = fixed-size array of values for that column
//  Best for:  both OLTP and OLAP — cache-friendly partial scans
// ============================================================
class PAXPage {
public:
    // Each row gets a mini-header in a central slot array
    struct RowMeta {
        SlotHeader hdr;
        bool       occupied;
    };

    // One "minipage" per column
    struct MiniPage {
        ColumnID           colID;
        std::vector<Value> data;   // parallel to rowMeta_
    };

    explicit PAXPage(PageID pid, const TableSchema& schema)
        : pageID_(pid), schema_(schema) {
        miniPages_.resize(schema.columns.size());
        for (ColumnID c = 0; c < schema.columns.size(); ++c)
            miniPages_[c].colID = c;
    }

    SlotID insert(const Tuple& t, TxnID txn, Timestamp bts) {
        if (t.size() != miniPages_.size())
            throw std::invalid_argument("PAX: tuple width != schema width");
        SlotID sid = static_cast<SlotID>(rowMeta_.size());
        rowMeta_.push_back({ { txn, bts, INF_TS, false }, true });
        for (ColumnID c = 0; c < miniPages_.size(); ++c)
            miniPages_[c].data.push_back(t[c]);
        return sid;
    }

    // Fast column projection within the page (no row reconstruction needed)
    std::vector<std::pair<SlotID, Value>> scanMiniPage(
            ColumnID col, Timestamp readTS) const {
        std::vector<std::pair<SlotID, Value>> result;
        if (col >= miniPages_.size()) return result;
        for (SlotID i = 0; i < rowMeta_.size(); ++i) {
            if (!rowMeta_[i].occupied) continue;
            if (!isVisible(rowMeta_[i].hdr, readTS)) continue;
            result.emplace_back(i, miniPages_[col].data[i]);
        }
        return result;
    }

    // Full row read (reconstruct from minipages — O(num_cols) cache lines)
    std::optional<Tuple> readRow(SlotID sid, Timestamp readTS) const {
        if (sid >= rowMeta_.size()) return std::nullopt;
        if (!rowMeta_[sid].occupied) return std::nullopt;
        if (!isVisible(rowMeta_[sid].hdr, readTS)) return std::nullopt;
        Tuple t;
        for (auto& mp : miniPages_)
            t.push_back(mp.data[sid]);
        return t;
    }

    // Full scan across all rows (returns visible tuples)
    std::vector<std::pair<SlotID, Tuple>> scan(Timestamp readTS) const {
        std::vector<std::pair<SlotID, Tuple>> result;
        for (SlotID i = 0; i < rowMeta_.size(); ++i) {
            auto t = readRow(i, readTS);
            if (t) result.emplace_back(i, *t);
        }
        return result;
    }

    SlotID update(SlotID oldSid, const Tuple& newData, TxnID txn,
                  Timestamp bts, Timestamp sealTS) {
        if (oldSid < rowMeta_.size()) {
            rowMeta_[oldSid].hdr.endTS  = sealTS;
            rowMeta_[oldSid].occupied   = false;
        }
        return insert(newData, txn, bts);
    }

    PageID id() const { return pageID_; }
    size_t numRows() const { return rowMeta_.size(); }

    double fillRatio() const {
        size_t used = rowMeta_.size() * miniPages_.size() * 12;
        return std::min(1.0, static_cast<double>(used) / PAGE_SIZE);
    }

    void dump(std::ostream& os = std::cout) const {
        os << "PAX Page " << pageID_ << " [" << rowMeta_.size() << " rows, "
           << std::fixed << std::setprecision(1) << fillRatio()*100 << "% full]\n";
        os << "  MiniPages:\n";
        for (auto& mp : miniPages_) {
            os << "    [" << schema_.columns[mp.colID].name << "]: ";
            for (size_t i = 0; i < mp.data.size(); ++i) {
                if (!rowMeta_[i].occupied) { os << "[DEL] "; continue; }
                os << mp.data[i].toString() << " ";
            }
            os << "\n";
        }
    }

private:
    bool isVisible(const SlotHeader& h, Timestamp ts) const {
        return h.beginTS <= ts && ts < h.endTS;
    }

    PageID             pageID_;
    const TableSchema& schema_;
    std::vector<RowMeta>  rowMeta_;
    std::vector<MiniPage> miniPages_;
};

} // namespace mvcc
