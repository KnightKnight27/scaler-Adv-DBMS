#pragma once

#include <cstdint>
#include <vector>

#include "catalog/catalog.h"
#include "common/types.h"

namespace minidb {

// In-memory columnar batch layer (column-major INT vectors over row-store heap).
struct ColumnarBatch {
    std::vector<Row> rows;
    std::vector<int64_t> int_column;
    int filter_col_idx = -1;

    void Clear();
    void Reserve(std::size_t n);
    void Append(const Row& row);
};

class VectorizedExecutor {
public:
    static std::vector<Row> BatchScan(HeapFile* heap, const TableSchema& schema,
                                      const std::vector<Predicate>& predicates);

    static bool CanUseColumnarFilter(const TableSchema& schema,
                                     const std::vector<Predicate>& predicates, int* col_idx,
                                     int64_t* target);

private:
    static std::vector<Row> FilterBatch(const TableSchema& schema, const ColumnarBatch& batch,
                                        const std::vector<Predicate>& predicates);
    static std::vector<Row> FilterColumnarIntEq(const ColumnarBatch& batch, int64_t target);
    static void ProcessBatch(ColumnarBatch& batch, const TableSchema& schema,
                             const std::vector<Predicate>& predicates, int64_t filter_target,
                             bool use_columnar, std::vector<Row>& output);
};

}  // namespace minidb
