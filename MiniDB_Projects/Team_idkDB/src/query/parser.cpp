#include "minidb/query/parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace minidb {
namespace {

std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return text;
}

int ParseInteger(std::string_view text) {
  int result{};
  const auto [ptr, error] =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || ptr != text.data() + text.size()) {
    throw std::invalid_argument("expected integer key, got: " +
                                std::string(text));
  }
  return result;
}

}  // namespace

Query Parser::Parse(std::string_view sql) const {
  while (!sql.empty() && std::isspace(static_cast<unsigned char>(sql.front()))) {
    sql.remove_prefix(1);
  }
  while (!sql.empty() &&
         (std::isspace(static_cast<unsigned char>(sql.back())) ||
          sql.back() == ';')) {
    sql.remove_suffix(1);
  }
  std::istringstream input{std::string(sql)};
  std::string command;
  input >> command;
  command = Upper(command);
  if (command == "EXIT" || command == "QUIT") {
    return {QueryType::Exit, {}, {}, 0, {}, false};
  }
  if (command == "HELP") return {QueryType::Help, {}, {}, 0, {}, false};
  if (command == "BEGIN") return {QueryType::Begin, {}, {}, 0, {}, false};
  if (command == "COMMIT") return {QueryType::Commit, {}, {}, 0, {}, false};
  if (command == "ABORT" || command == "ROLLBACK") {
    return {QueryType::Abort, {}, {}, 0, {}, false};
  }

  std::string table;
  if (!(input >> table)) throw std::invalid_argument("missing table name");
  if (command == "INSERT") {
    std::string key_text;
    if (!(input >> key_text)) throw std::invalid_argument("missing key");
    std::string value;
    std::getline(input >> std::ws, value);
    if (value.empty()) throw std::invalid_argument("missing value");
    return {QueryType::Insert, table, {}, ParseInteger(key_text), value, false};
  }

  if (command != "SELECT" && command != "DELETE") {
    throw std::invalid_argument("supported commands: INSERT, SELECT, DELETE");
  }
  std::string maybe_join;
  std::string join_table;
  std::string where;
  std::string predicate;
  if (!(input >> maybe_join)) {
    throw std::invalid_argument("expected WHERE id=<integer>");
  }
  if (Upper(maybe_join) == "JOIN") {
    std::string join_keyword;
    if (!(input >> join_table >> join_keyword >> predicate)) {
      throw std::invalid_argument(
          "expected SELECT table1 JOIN table2 ON table1.id=table2.id");
    }
    if (Upper(join_keyword) == "ON") {
      const auto equals = predicate.find('=');
      if (equals == std::string::npos) {
        throw std::invalid_argument(
            "expected SELECT table1 JOIN table2 ON table1.id=table2.id");
      }
      const auto left = predicate.substr(0, equals);
      const auto right = predicate.substr(equals + 1);
      const auto expected_left = table + ".id";
      const auto expected_right = join_table + ".id";
      if (Upper(left) != Upper(expected_left) ||
          Upper(right) != Upper(expected_right)) {
        throw std::invalid_argument("only equi-join ON table1.id=table2.id is supported");
      }
      return {QueryType::Join, table, join_table, 0, {}, true};
    }
    where = join_keyword;
    if (Upper(where) != "WHERE") {
      throw std::invalid_argument(
          "expected SELECT table1 JOIN table2 ON table1.id=table2.id");
    }
  } else {
    where = maybe_join;
    if (!(input >> predicate) || Upper(where) != "WHERE") {
    throw std::invalid_argument("expected WHERE id=<integer>");
    }
  }
  const auto equals = predicate.find('=');
  if (equals == std::string::npos ||
      Upper(predicate.substr(0, equals)) != "ID") {
    throw std::invalid_argument("only primary-key predicate id=<integer> is supported");
  }
  const int key = ParseInteger(std::string_view(predicate).substr(equals + 1));
  if (command == "SELECT" && !join_table.empty()) {
    return {QueryType::Join, table, join_table, key, {}, false};
  }
  return {command == "SELECT" ? QueryType::Select : QueryType::Delete, table,
          {}, key, {}, false};
}

}  // namespace minidb
