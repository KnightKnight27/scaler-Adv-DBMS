#include "minidb/sql/parser.h"

#include <algorithm>
#include <array>

namespace minidb {
namespace {

std::string StripSemicolon(std::string_view sql) {
  std::string out = Trim(sql);
  if (!out.empty() && out.back() == ';') {
    out.pop_back();
  }
  return Trim(out);
}

std::size_t FindKeyword(std::string_view sql, std::string_view keyword, std::size_t start = 0) {
  std::string upper = ToUpper(sql);
  std::string needle = ToUpper(keyword);
  return upper.find(needle, start);
}

bool StartsWithKeyword(std::string_view sql, std::string_view keyword) {
  std::string upper = ToUpper(Trim(sql));
  std::string needle = ToUpper(keyword);
  return upper.rfind(needle, 0) == 0;
}

std::string RequireIdentifier(std::string_view text, std::string_view context) {
  std::string id = Trim(text);
  if (id.empty() || id.find(' ') != std::string::npos || id.find('\t') != std::string::npos) {
    throw MiniDbError("expected identifier for " + std::string(context));
  }
  return id;
}

}  // namespace

SqlStatement SqlParser::Parse(std::string_view sql) const {
  std::string cleaned = StripSemicolon(sql);
  if (StartsWithKeyword(cleaned, "INSERT")) {
    return SqlStatement{StatementType::Insert, ParseInsert(cleaned), std::nullopt, std::nullopt};
  }
  if (StartsWithKeyword(cleaned, "SELECT")) {
    return SqlStatement{StatementType::Select, std::nullopt, ParseSelect(cleaned), std::nullopt};
  }
  if (StartsWithKeyword(cleaned, "DELETE")) {
    return SqlStatement{StatementType::Delete, std::nullopt, std::nullopt, ParseDelete(cleaned)};
  }
  throw MiniDbError("unsupported SQL statement");
}

InsertStatement SqlParser::ParseInsert(std::string_view sql) const {
  constexpr std::string_view prefix = "INSERT INTO";
  if (!StartsWithKeyword(sql, prefix)) {
    throw MiniDbError("expected INSERT INTO statement");
  }

  std::size_t values_pos = FindKeyword(sql, " VALUES ");
  if (values_pos == std::string::npos) {
    throw MiniDbError("INSERT statement must contain VALUES");
  }

  std::string table = RequireIdentifier(sql.substr(prefix.size(), values_pos - prefix.size()), "INSERT table");
  Row values = ParseValues(sql.substr(values_pos + 8));
  return InsertStatement{std::move(table), std::move(values)};
}

SelectStatement SqlParser::ParseSelect(std::string_view sql) const {
  constexpr std::string_view prefix = "SELECT";
  std::size_t from_pos = FindKeyword(sql, " FROM ");
  if (from_pos == std::string::npos) {
    throw MiniDbError("SELECT statement must contain FROM");
  }

  std::size_t where_pos = FindKeyword(sql, " WHERE ", from_pos + 6);
  std::string table;
  Predicate where;
  if (where_pos == std::string::npos) {
    table = RequireIdentifier(sql.substr(from_pos + 6), "SELECT table");
  } else {
    table = RequireIdentifier(sql.substr(from_pos + 6, where_pos - (from_pos + 6)), "SELECT table");
    where = ParsePredicate(sql.substr(where_pos + 7));
  }

  return SelectStatement{ParseColumns(sql.substr(prefix.size(), from_pos - prefix.size())), std::move(table), where};
}

DeleteStatement SqlParser::ParseDelete(std::string_view sql) const {
  constexpr std::string_view prefix = "DELETE FROM";
  if (!StartsWithKeyword(sql, prefix)) {
    throw MiniDbError("expected DELETE FROM statement");
  }

  std::size_t where_pos = FindKeyword(sql, " WHERE ");
  std::string table;
  Predicate where;
  if (where_pos == std::string::npos) {
    table = RequireIdentifier(sql.substr(prefix.size()), "DELETE table");
  } else {
    table = RequireIdentifier(sql.substr(prefix.size(), where_pos - prefix.size()), "DELETE table");
    where = ParsePredicate(sql.substr(where_pos + 7));
  }
  return DeleteStatement{std::move(table), where};
}

Predicate SqlParser::ParsePredicate(std::string_view text) const {
  const std::array<std::string_view, 4> operators{"!=", ">=", "<=", "="};
  for (std::string_view op : operators) {
    std::size_t pos = text.find(op);
    if (pos == std::string::npos) {
      continue;
    }
    std::string column = RequireIdentifier(text.substr(0, pos), "predicate column");
    std::string value = ParseLiteral(text.substr(pos + op.size()));
    return Predicate{std::move(column), std::string(op), std::move(value), false};
  }
  throw MiniDbError("WHERE predicate must use one of =, !=, >=, <=");
}

Row SqlParser::ParseValues(std::string_view text) const {
  std::string values = Trim(text);
  if (values.size() < 2 || values.front() != '(' || values.back() != ')') {
    throw MiniDbError("VALUES must be enclosed in parentheses");
  }

  Row row;
  for (const auto& part : SplitCsv(std::string_view(values).substr(1, values.size() - 2))) {
    row.push_back(ParseLiteral(part));
  }
  if (row.empty()) {
    throw MiniDbError("VALUES must contain at least one value");
  }
  return row;
}

std::vector<std::string> SqlParser::ParseColumns(std::string_view text) const {
  std::vector<std::string> columns;
  for (const auto& part : SplitCsv(text)) {
    std::string column = Trim(part);
    if (column.empty()) {
      throw MiniDbError("SELECT column list contains an empty column");
    }
    columns.push_back(column);
  }
  if (columns.empty()) {
    throw MiniDbError("SELECT must include at least one column");
  }
  return columns;
}

std::string SqlParser::ParseLiteral(std::string_view text) const {
  std::string value = Trim(text);
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    value = value.substr(1, value.size() - 2);
  }
  return Unescape(value);
}

}  // namespace minidb
