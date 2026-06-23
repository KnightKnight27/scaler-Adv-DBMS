#pragma once
#include "storage/tuple.h"
#include <memory>

namespace minidb {

// Base class for all Volcano-style executors
class Executor {
public:
    virtual ~Executor() = default;

    // Initialize the executor (open files, initialize variables)
    virtual void Init() = 0;

    // Retrieve the next tuple. Returns true if a tuple was found.
    virtual bool Next(Tuple* tuple) = 0;
};

} // namespace minidb
