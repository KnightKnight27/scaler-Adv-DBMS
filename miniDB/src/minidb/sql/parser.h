#pragma once

#include <optional>
#include <string>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

enum class StatementType { Insert, Select, Delete };

struct InsertStatement {
  std::string table;
  Row values;
};

struct SelectStatement {
  std::vector<std::string> columns;
  std::string table;
  Predicate where;
};

struct DeleteStatement {
  std::string table;
  Predicate where;
};

struct SqlStatement {
  StatementType type;
  std::optional<InsertStatement> insert;
  std::optional<SelectStatement> select;
  std::optional<DeleteStatement> delete_from;
};

class SqlParser {
 public:
  SqlStatement Parse(std::string_view sql) const;

 private:
  InsertStatement ParseInsert(std::string_view sql) const;
  SelectStatement ParseSelect(std::string_view sql) const;
  DeleteStatement ParseDelete(std::string_view sql) const;
  Predicate ParsePredicate(std::string_view text) const;
  Row ParseValues(std::string_view text) const;
  std::vector<std::string> ParseColumns(std::string_view text) const;
  std::string ParseLiteral(std::string_view text) const;
};

}  // namespace minidb
