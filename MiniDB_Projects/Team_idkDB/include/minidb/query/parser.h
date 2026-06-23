#pragma once

#include <string>
#include <string_view>

namespace minidb {

enum class QueryType { Insert, Select, Delete, Join, Begin, Commit, Abort, Exit, Help };

struct Query {
  QueryType type;
  std::string table;
  std::string join_table;
  int key{};
  std::string value;
  bool join_all_on_id{false};
};

class Parser {
 public:
  Query Parse(std::string_view sql) const;
};

}  // namespace minidb
