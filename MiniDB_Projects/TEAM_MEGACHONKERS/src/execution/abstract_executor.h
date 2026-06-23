#pragma once

#include "common/types.h"
#include "catalog/schema.h"
#include "execution/executor_context.h"

namespace minidb {

class AbstractExecutor {
protected:
    ExecutorContext* context_;

public:
    explicit AbstractExecutor(ExecutorContext* context) : context_(context) {}
    
    // Virtual destructor is critical for base classes to prevent memory leaks
    virtual ~AbstractExecutor() = default;

    // Initializes the executor. 
    // For a SeqScan, this opens the table. For a Join, this initializes the child nodes.
    virtual void Init() = 0;

    // Pulls the next row from the operator.
    // Returns true if a row was successfully retrieved, false if we hit EOF.
    virtual bool Next(Row* row) = 0;

    // Returns the schema of the tuples this executor produces.
    // This allows parent nodes to understand the data coming from child nodes.
    virtual const Schema* GetOutputSchema() const = 0;
};

} // namespace minidb