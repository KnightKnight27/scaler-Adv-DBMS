#pragma once
#include "execution/executor.h"
#include <memory>

namespace minidb {

class NestedLoopJoinExecutor : public Executor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<Executor> left_child, std::unique_ptr<Executor> right_child);

    void Init() override;
    bool Next(Tuple* tuple) override;

private:
    std::unique_ptr<Executor> left_child_;
    std::unique_ptr<Executor> right_child_;
    
    Tuple current_left_tuple_;
    bool has_left_tuple_;
};

} // namespace minidb
