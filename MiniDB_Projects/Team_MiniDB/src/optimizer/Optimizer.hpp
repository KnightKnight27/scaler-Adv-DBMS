#pragma once

#include <string>

using namespace std;

#include "catalog/Catalog.hpp"
#include "common/Types.hpp"
#include "parser/Parser.hpp"

namespace minidb {

enum class ScanType { TABLE_SCAN, INDEX_SCAN };

struct QueryPlan {
    ScanType scan = ScanType::TABLE_SCAN;
    string table_name;
    WhereClause where;
    bool has_where = false;
    JoinClause join;
    bool has_join = false;
    string join_outer_table;
    string join_inner_table;
    double estimated_cost = 0.0;
    double selectivity = 1.0;
};

class Optimizer {
public:
    explicit Optimizer(Catalog& catalog);
    QueryPlan buildSelectPlan(const ParsedStatement& stmt);

private:
    Catalog& catalog_;
    double estimateSelectivity(const TableDef& table, const WhereClause& where);
    ScanType chooseScan(const TableDef& table, const WhereClause& where, bool has_where);
};

}  // namespace minidb
