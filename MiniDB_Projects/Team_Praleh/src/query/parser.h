#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// parser.h  –  tiny SQL-like command parser
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <cstdint>

namespace minidb {

enum class StmtType { INSERT, SELECT, DELETE, SHOW, BEGIN, COMMIT, ABORT, SELECT_JOIN, INVALID };
enum class OpType { EQUAL, GREATER, NONE };
enum class ColType { ID, VALUE, NONE };

struct Statement {
    StmtType type  = StmtType::INVALID;
    int32_t  id    = 0;   // primary key; -1 means wildcard
    int32_t  value = 0;   // used only by INSERT
    ColType  where_col = ColType::NONE;
    OpType   where_op  = OpType::NONE;
    int32_t  where_val = 0; // comparison constant
};

/**
 * Parser::Parse – tokenises one input line into a Statement.
 *
 * Grammar:
 *   INSERT <id> <value>
 *   SELECT <id> | SELECT *
 *   DELETE <id>
 *   SHOW              (full scan alias)
 *   BEGIN / COMMIT / ABORT
 */
class Parser {
public:
    static Statement Parse(const std::string& line);
};

} // namespace minidb
