#pragma once

#include "catalog/Catalog.hpp"
#include "common/Types.hpp"
#include "optimizer/Optimizer.hpp"

namespace minidb {

class Executor {
public:
    explicit Executor(Catalog& catalog, bool use_batch_mode = false);
    RowList executeSelect(const QueryPlan& plan);
    bool evaluatePredicate(const Row& row, const WhereClause& where, const TableDef& table);
    void setBatchMode(bool enabled) { use_batch_mode_ = enabled; }

private:
    Catalog& catalog_;
    bool use_batch_mode_;
    RowList tableScan(const string& table, const WhereClause& where, bool has_where);
    RowList indexScan(const string& table, const WhereClause& where);
    RowList nestedLoopJoin(const JoinClause& join, const QueryPlan& plan);
    bool compareValues(const Value& left, CompareOp op, const Value& right);
};

}  // namespace minidb
