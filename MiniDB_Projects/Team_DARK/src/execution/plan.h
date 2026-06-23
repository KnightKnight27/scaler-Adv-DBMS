#pragma once

#include "parser/ast.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minidb {

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    FILTER,
    NESTED_LOOP_JOIN,
    PROJECT,
    INSERT,
    DELETE
};

struct IndexScanParams {
    std::string column;
    std::string op;
    int64_t bound = 0;
};

struct FilterParams {
    std::unique_ptr<Expr> predicate;
};

struct ProjectParams {
    std::string column;
};

struct JoinParams {
    std::string left_table;
    std::string right_table;
    std::string left_column;
    std::string right_column;
};

struct InsertParams {
    std::string table;
    std::vector<std::string> columns;
    std::vector<Value> values;
};

struct DeleteParams {
    std::string table;
    std::unique_ptr<Expr> predicate;
};

struct PlanNode {
    PlanType type = PlanType::SEQ_SCAN;
    std::string table;
    std::vector<std::unique_ptr<PlanNode>> children;

    IndexScanParams index_scan;
    FilterParams filter;
    ProjectParams project;
    JoinParams join;
    InsertParams insert;
    DeleteParams delete_plan;
};

}  // namespace minidb
