#pragma once

#include "execution/operators.h"

namespace minidb::bench {

// Input preparation happens before timing. Both sources own the same rows.
class BatchSource : public Operator {
public:
    BatchSource(std::vector<Row> rows, Schema schema);
    void Open() override;
    TupleBatch Next() override;
    void Close() override {}
    Schema GetOutputSchema() const override { return schema_; }

private:
    std::vector<TupleBatch> batches_;
    Schema schema_;
    size_t cursor_ = 0;
};

class RowSource : public RowOperator {
public:
    RowSource(std::vector<Row> rows, Schema schema);
    void Open() override;
    bool Next(Row& row) override;
    void Close() override {}
    Schema GetOutputSchema() const override { return schema_; }

private:
    std::vector<Row> rows_;
    Schema schema_;
    size_t cursor_ = 0;
};

} // namespace minidb::bench
