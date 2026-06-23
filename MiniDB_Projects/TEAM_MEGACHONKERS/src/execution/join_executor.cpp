#include "execution/join_executor.h"
#include "common/logger.h"

namespace minidb {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext* context,
                                               std::unique_ptr<AbstractExecutor> left_child,
                                               std::unique_ptr<AbstractExecutor> right_child,
                                               uint32_t left_col_idx,
                                               uint32_t right_col_idx)
    : AbstractExecutor(context),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)),
      left_col_idx_(left_col_idx),
      right_col_idx_(right_col_idx) {

    // Construct the output schema: Left Schema + Right Schema
    std::vector<Column> combined_columns;
    const Schema* left_schema = left_child_->GetOutputSchema();
    const Schema* right_schema = right_child_->GetOutputSchema();

    if (left_schema && right_schema) {
        for (const auto& col : left_schema->GetColumns()) {
            combined_columns.push_back(col);
        }
        for (const auto& col : right_schema->GetColumns()) {
            combined_columns.push_back(col);
        }
        output_schema_ = std::make_unique<Schema>(combined_columns);
    }
}

void NestedLoopJoinExecutor::Init() {
    left_child_->Init();
    right_child_->Init();
    
    // Fetch the very first row from the left child to kick off the outer loop
    has_left_row_ = left_child_->Next(&current_left_row_);
    LOG_INFO("NestedLoopJoinExecutor initialized.");
}

bool NestedLoopJoinExecutor::Next(Row* row) {
    while (has_left_row_) {
        Row right_row;
        
        // Inner Loop: Scan the right child for a match
        while (right_child_->Next(&right_row)) {
            // Robustness Check: Ensure we don't access indexes out of bounds
            if (left_col_idx_ < current_left_row_.columns.size() && 
                right_col_idx_ < right_row.columns.size()) {
                
                // Evaluate the Equi-Join Predicate
                if (current_left_row_.columns[left_col_idx_] == right_row.columns[right_col_idx_]) {
                    
                    // Match found! Concatenate the left and right rows into a single output tuple
                    row->columns = current_left_row_.columns;
                    row->columns.insert(row->columns.end(), 
                                        right_row.columns.begin(), 
                                        right_row.columns.end());
                    
                    return true; // Yield this combined row up the pipeline
                }
            }
        }

        // We reached the end of the right child. 
        // We must reset the right child's iterator to the beginning.
        right_child_->Init();
        
        // Advance the outer loop to the next left row
        has_left_row_ = left_child_->Next(&current_left_row_);
    }

    return false; // EOF: Both loops exhausted
}

} // namespace minidb