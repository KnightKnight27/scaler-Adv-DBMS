#pragma once
#include "common/types.h"
#include <memory>

namespace minidb {

class Operator {
public:
    virtual ~Operator() = default;
    
    virtual void Open() = 0;
    virtual bool Next(Tuple& out) = 0;
    virtual void Close() = 0;
    
    virtual const Schema& get_schema() const = 0;
};

} // namespace minidb
