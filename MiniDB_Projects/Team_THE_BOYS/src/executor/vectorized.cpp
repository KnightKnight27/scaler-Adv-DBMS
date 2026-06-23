#include "executor/vectorized.h"

#include "executor/execution_metrics.h"

namespace minidb {

void ColumnarBatch::Clear() {
    rows.clear();
    int_column.clear();
}

void ColumnarBatch::Reserve(std::size_t n) {
    rows.reserve(n);
    if (filter_col_idx >= 0) {
        int_column.reserve(n);
    }
}

void ColumnarBatch::Append(const Row& row) {
    rows.push_back(row);
    if (filter_col_idx >= 0) {
        const Value& v = row.Get(static_cast<std::size_t>(filter_col_idx));
        if (v.type == ValueType::INT) {
            int_column.push_back(std::get<int64_t>(v.data));
        } else {
            int_column.push_back(INT64_MIN);
        }
    }
}

bool VectorizedExecutor::CanUseColumnarFilter(const TableSchema& schema,
                                              const std::vector<Predicate>& predicates,
                                              int* col_idx, int64_t* target) {
    if (predicates.size() != 1 || predicates.front().op != CompareOp::EQ) return false;
    int idx = schema.ColumnIndex(predicates.front().column);
    if (idx < 0 || idx >= static_cast<int>(schema.columns.size())) return false;
    if (schema.columns[static_cast<std::size_t>(idx)].type != ValueType::INT) return false;
    if (predicates.front().value.type != ValueType::INT) return false;
    *col_idx = idx;
    *target = std::get<int64_t>(predicates.front().value.data);
    return true;
}

std::vector<Row> VectorizedExecutor::FilterColumnarIntEq(const ColumnarBatch& batch,
                                                         int64_t target) {
    std::vector<Row> out;
    out.reserve(batch.rows.size() / 16 + 1);
    const std::size_t n = batch.int_column.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (batch.int_column[i] == target) {
            out.push_back(batch.rows[i]);
        }
    }
    auto& metrics = ExecutionMetricsHolder::Get();
    metrics.columnar_vector_bytes += n * sizeof(int64_t);
    metrics.used_columnar_filter = true;
    return out;
}

std::vector<Row> VectorizedExecutor::FilterBatch(const TableSchema& schema,
                                                 const ColumnarBatch& batch,
                                                 const std::vector<Predicate>& predicates) {
    std::vector<Row> out;
    out.reserve(batch.rows.size() / 4 + 1);
    for (const auto& row : batch.rows) {
        bool ok = true;
        for (const auto& pred : predicates) {
            int idx = schema.ColumnIndex(pred.column);
            if (!CompareValues(row.Get(static_cast<std::size_t>(idx)), pred.op, pred.value)) {
                ok = false;
                break;
            }
        }
        if (ok) out.push_back(row);
    }
    return out;
}

void VectorizedExecutor::ProcessBatch(ColumnarBatch& batch, const TableSchema& schema,
                                      const std::vector<Predicate>& predicates,
                                      int64_t filter_target, bool use_columnar,
                                      std::vector<Row>& output) {
    auto& metrics = ExecutionMetricsHolder::Get();
    metrics.batches_processed++;
    metrics.tuples_scanned += batch.rows.size();

    std::vector<Row> filtered;
    if (use_columnar) {
        filtered = FilterColumnarIntEq(batch, filter_target);
    } else {
        filtered = FilterBatch(schema, batch, predicates);
    }
    metrics.tuples_output += filtered.size();
    output.insert(output.end(), filtered.begin(), filtered.end());
    batch.Clear();
}

std::vector<Row> VectorizedExecutor::BatchScan(HeapFile* heap, const TableSchema& schema,
                                               const std::vector<Predicate>& predicates) {
    ExecutionMetricsHolder::Reset();

    int filter_col_idx = -1;
    int64_t filter_target = 0;
    bool use_columnar = CanUseColumnarFilter(schema, predicates, &filter_col_idx, &filter_target);

    ColumnarBatch batch;
    batch.filter_col_idx = use_columnar ? filter_col_idx : -1;
    batch.Reserve(BATCH_SIZE);

    std::vector<Row> output;
    for (const Rid& rid : heap->ScanAll()) {
        if (auto row = heap->GetTuple(rid)) {
            batch.Append(*row);
            if (batch.rows.size() >= BATCH_SIZE) {
                ProcessBatch(batch, schema, predicates, filter_target, use_columnar, output);
            }
        }
    }
    if (!batch.rows.empty()) {
        ProcessBatch(batch, schema, predicates, filter_target, use_columnar, output);
    }
    return output;
}

}  // namespace minidb
