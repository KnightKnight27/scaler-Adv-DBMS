#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/types.h"

namespace minidb {

enum class StmtType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE_STMT,
    BEGIN_TXN,
    COMMIT,
    ROLLBACK,
    CHECKPOINT,
    CRASH,
    USE_BATCH,
    USE_ROW
};

struct CreateTableStmt {
    std::string table;
    std::vector<ColumnDef> columns;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> columns;
    std::vector<Value> values;
};

enum class AggFunc { COUNT };

struct AggregateExpr {
    AggFunc func = AggFunc::COUNT;
    std::string column;  // empty for COUNT(*)
    std::string alias;
};

struct SelectStmt {
    std::vector<std::string> columns;
    std::vector<AggregateExpr> aggregates;
    std::vector<std::string> group_by;
    std::vector<std::string> tables;
    std::vector<Predicate> predicates;
    std::vector<JoinSpec> joins;
};

struct DeleteStmt {
    std::string table;
    std::vector<Predicate> predicates;
};

struct ParsedStatement {
    StmtType type;
    CreateTableStmt create_table;
    InsertStmt insert;
    SelectStmt select;
    DeleteStmt delete_stmt;
    bool use_batch = false;
};

class SqlParser {
public:
    ParsedStatement Parse(const std::string& sql) const;
};

}  // namespace minidb
