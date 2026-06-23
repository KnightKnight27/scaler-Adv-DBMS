#pragma once
#include "parser/ast.h"
#include "catalog/catalog.h"
#include <string>
#include <optional>

enum class ScanType { TABLE_SCAN, INDEX_SCAN };

struct QueryPlan {
    ScanType    scan;
    std::string table;
    Expression* filter = nullptr;

    int         pk_val = 0;

    bool        has_join      = false;
    std::string join_table;
    std::string join_left_col;
    std::string join_right_col;
};

class Optimizer {
public:
    explicit Optimizer(const Catalog& catalog);
    QueryPlan plan(const SelectStmt& stmt) const;

private:
    const Catalog& catalog_;
    std::optional<int> extract_pk_eq(const Expression* expr,
                                     const std::string& pk_col) const;
};
