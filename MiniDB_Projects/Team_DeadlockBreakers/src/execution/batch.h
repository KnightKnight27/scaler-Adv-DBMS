#pragma once

#include "common/types.h"
#include <vector>

namespace minidb {

/*
 * TupleBatch: a vector of rows passed between operators (Track A — Vectorized).
 * Instead of processing one row at a time, operators exchange batches of rows.
 */
struct TupleBatch {
    std::vector<Row> rows;

    TupleBatch() { rows.reserve(BATCH_SIZE); }

    bool IsEmpty() const { return rows.empty(); }
    bool IsFull()  const { return rows.size() >= BATCH_SIZE; }
    size_t Size()  const { return rows.size(); }

    void AddRow(const Row& row) {
        rows.push_back(row);
    }

    void AddRow(Row&& row) {
        rows.push_back(std::move(row));
    }

    void Clear() { rows.clear(); }
};

} // namespace minidb
