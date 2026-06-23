#pragma once

#include "execution/abstract_executor.h"
#include <memory>
#include <vector>

namespace minidb {

// Projects a subset (and/or reordering) of its child's columns -- the physical
// operator behind `SELECT col_a, col_c FROM ...`. It pulls a full row from the
// child and emits a new row containing only the requested column indices, in
// the requested order. It also derives a matching output Schema so callers
// (e.g. the REPL printing headers) see the projected columns, not the base
// table's.
class ProjectionExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_executor_;
    std::vector<uint32_t> output_indices_;
    std::unique_ptr<Schema> output_schema_;

public:
    ProjectionExecutor(ExecutorContext* context,
                       std::unique_ptr<AbstractExecutor> child,
                       std::vector<uint32_t> output_indices)
        : AbstractExecutor(context),
          child_executor_(std::move(child)),
          output_indices_(std::move(output_indices)) {
        // Derive the projected schema from the child's schema up front so it is
        // available before Init() (the REPL reads it to print headers).
        const Schema* child_schema = child_executor_->GetOutputSchema();
        if (child_schema != nullptr) {
            std::vector<Column> projected;
            for (uint32_t idx : output_indices_) {
                if (idx < child_schema->GetColumnCount()) {
                    projected.push_back(child_schema->GetColumn(idx));
                }
            }
            output_schema_ = std::make_unique<Schema>(projected);
        }
    }

    void Init() override { child_executor_->Init(); }

    bool Next(Row* row) override {
        Row child_row;
        if (!child_executor_->Next(&child_row)) return false;

        Row projected;
        projected.columns.reserve(output_indices_.size());
        for (uint32_t idx : output_indices_) {
            if (idx < child_row.columns.size()) {
                projected.columns.push_back(child_row.columns[idx]);
            } else {
                projected.columns.emplace_back(); // defensive: missing column -> empty
            }
        }
        *row = std::move(projected);
        return true;
    }

    const Schema* GetOutputSchema() const override { return output_schema_.get(); }
};

} // namespace minidb
