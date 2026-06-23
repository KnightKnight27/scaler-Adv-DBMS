#include "execution/nested_loop_join.h"
#include <cstring>
#include <string>

namespace minidb {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::unique_ptr<Executor> left_child, std::unique_ptr<Executor> right_child)
    : left_child_(std::move(left_child)), right_child_(std::move(right_child)) {}

void NestedLoopJoinExecutor::Init() {
    left_child_->Init();
    right_child_->Init();
    has_left_tuple_ = left_child_->Next(&current_left_tuple_);
}

bool NestedLoopJoinExecutor::Next(Tuple* tuple) {
    while (has_left_tuple_) {
        Tuple right_tuple;
        while (right_child_->Next(&right_tuple)) {
            // Simplest Join Condition: Left.id == Right.id
            if (current_left_tuple_.id == right_tuple.id) {
                tuple->id = current_left_tuple_.id;
                
                // Combine names for demonstration
                std::string combined = std::string(current_left_tuple_.name) + "-" + std::string(right_tuple.name);
                strncpy(tuple->name, combined.c_str(), sizeof(tuple->name) - 1);
                tuple->name[sizeof(tuple->name) - 1] = '\0';
                
                return true; // Return matched tuple
            }
        }
        
        // Right child exhausted, advance left child and reset right child
        has_left_tuple_ = left_child_->Next(&current_left_tuple_);
        if (has_left_tuple_) {
            right_child_->Init();
        }
    }
    return false;
}

} // namespace minidb
