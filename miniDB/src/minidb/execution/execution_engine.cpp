#include "minidb/execution/execution_engine.h"

#include <algorithm>
#include "minidb/optimizer/optimizer.h"

namespace minidb {

ExecutionEngine::ExecutionEngine(BufferPool& buffer) : buffer_(buffer) {}

void ExecutionEngine::CreateTable(std::string name, std::vector<Column> columns) {
  if (name.empty()) {
    throw MiniDbError("table name cannot be empty");
  }
  if (columns.empty()) {
    throw MiniDbError("table must contain at least one column");
  }
  if (tables_.find(name) != tables_.end()) {
    throw MiniDbError("table already exists: " + name);
  }

  std::size_t primary_key_column = 0;
  for (std::size_t i = 0; i < columns.size(); ++i) {
    if (columns[i].primary_key) {
      primary_key_column = i;
      break;
    }
  }

  PageId first_page = HeapTable::Create(buffer_);
  TableInfo table{name, std::move(columns), first_page, primary_key_column, BPlusTree{}};
  std::string table_name = table.name;
  tables_.emplace(std::move(table_name), std::move(table));
}

QueryResult ExecutionEngine::Execute(std::string_view sql) {
  SqlStatement statement = parser_.Parse(sql);
  switch (statement.type) {
    case StatementType::Insert:
      return ExecuteInsert(*statement.insert);
    case StatementType::Select:
      return ExecuteSelect(*statement.select);
    case StatementType::Delete:
      return ExecuteDelete(*statement.delete_from);
  }
  throw MiniDbError("unknown statement type");
}

QueryResult ExecutionEngine::ExecuteInsert(const InsertStatement& statement) {
  TableInfo& table = Table(statement.table);
  if (statement.values.size() != table.columns.size()) {
    throw MiniDbError("INSERT value count does not match table schema");
  }

  std::string key = statement.values[table.primary_key_column];
  if (table.primary_index.Search(key).has_value()) {
    throw MiniDbError("duplicate primary key: " + key);
  }

  Rid rid = Heap(table).Insert(EncodeRow(statement.values));
  table.primary_index.Insert(std::move(key), rid);
  return QueryResult{{}, {}, 1};
}

QueryResult ExecutionEngine::ExecuteSelect(const SelectStatement& statement) {
  if (statement.join.has_value()) {
    return ExecuteJoinSelect(statement);
  }

  TableInfo& table = Table(statement.table);
  QueryResult result;
  result.columns = statement.count_star ? std::vector<std::string>{"count"} : statement.columns;

  std::vector<std::pair<Rid, Row>> candidates;
  bool is_pk = !statement.where.empty && ColumnIndex(table, statement.where.column) == table.primary_key_column;
  bool used_index = Optimizer::ShouldUseIndexScan(statement.where, is_pk);
  double selectivity = Optimizer::EstimateSelectivity(statement.where, is_pk);
  (void)selectivity; // For cost-based decisions in a more complex engine
  if (used_index) {
    auto rid = table.primary_index.Search(statement.where.value);
    if (rid.has_value()) {
      auto encoded = Heap(table).Get(*rid);
      if (encoded.has_value()) {
        candidates.push_back({*rid, DecodeRow(*encoded)});
      }
    }
  } else {
    candidates = ReadRows(table);
  }

  std::size_t count = 0;
  for (const auto& [rid, row] : candidates) {
    (void)rid;
    if (!MatchesPredicate(table, row, statement.where)) {
      continue;
    }
    if (statement.count_star) {
      ++count;
    } else {
      result.rows.push_back(ProjectRow(table, row, statement.columns));
    }
  }

  if (statement.count_star) {
    result.rows.push_back({std::to_string(count)});
  }
  return result;
}

QueryResult ExecutionEngine::ExecuteDelete(const DeleteStatement& statement) {
  TableInfo& table = Table(statement.table);
  QueryResult result;

  bool is_pk = !statement.where.empty && ColumnIndex(table, statement.where.column) == table.primary_key_column;
  bool used_index = Optimizer::ShouldUseIndexScan(statement.where, is_pk);
  if (used_index) {
    auto rid = table.primary_index.Search(statement.where.value);
    if (!rid.has_value()) {
      return result;
    }
    if (Heap(table).Delete(*rid)) {
      table.primary_index.Delete(statement.where.value);
      result.affected_rows = 1;
    }
    return result;
  }

  for (const auto& [rid, row] : ReadRows(table)) {
    if (MatchesPredicate(table, row, statement.where) && Heap(table).Delete(rid)) {
      table.primary_index.Delete(row[table.primary_key_column]);
      ++result.affected_rows;
    }
  }
  return result;
}

QueryResult ExecutionEngine::ExecuteJoinSelect(const SelectStatement& statement) {
  TableInfo& left_table = Table(statement.table);
  TableInfo& right_table = Table(statement.join->table);
  QueryResult result;
  result.columns = statement.count_star ? std::vector<std::string>{"count"} : statement.columns;

  std::size_t left_join_col = ColumnIndex(left_table, statement.join->left_column);
  std::size_t right_join_col = ColumnIndex(right_table, statement.join->right_column);
  std::size_t count = 0;

  auto left_rows = ReadRows(left_table);
  auto right_rows = ReadRows(right_table);
  for (const auto& [left_rid, left_row] : left_rows) {
    (void)left_rid;
    for (const auto& [right_rid, right_row] : right_rows) {
      (void)right_rid;
      if (left_row[left_join_col] != right_row[right_join_col]) {
        continue;
      }
      if (!MatchesQualifiedPredicate(left_table, left_row, right_table, right_row, statement.where)) {
        continue;
      }
      if (statement.count_star) {
        ++count;
      } else {
        result.rows.push_back(ProjectJoinedRow(left_table, left_row, right_table, right_row, statement.columns));
      }
    }
  }

  if (statement.count_star) {
    result.rows.push_back({std::to_string(count)});
  }
  return result;
}

ExecutionEngine::TableInfo& ExecutionEngine::Table(std::string_view name) {
  auto it = tables_.find(std::string(name));
  if (it == tables_.end()) {
    throw MiniDbError("unknown table: " + std::string(name));
  }
  return it->second;
}

const ExecutionEngine::TableInfo& ExecutionEngine::Table(std::string_view name) const {
  auto it = tables_.find(std::string(name));
  if (it == tables_.end()) {
    throw MiniDbError("unknown table: " + std::string(name));
  }
  return it->second;
}

HeapTable ExecutionEngine::Heap(const TableInfo& table) { return HeapTable(buffer_, table.first_page); }

std::vector<std::pair<Rid, Row>> ExecutionEngine::ReadRows(TableInfo& table) {
  std::vector<std::pair<Rid, Row>> rows;
  for (auto& [rid, encoded] : Heap(table).Scan()) {
    rows.push_back({rid, DecodeRow(encoded)});
  }
  return rows;
}

std::size_t ExecutionEngine::ColumnIndex(const TableInfo& table, std::string_view column) const {
  std::string name = UnqualifiedColumn(column);
  for (std::size_t i = 0; i < table.columns.size(); ++i) {
    if (table.columns[i].name == name) {
      return i;
    }
  }
  throw MiniDbError("unknown column: " + std::string(column));
}

bool ExecutionEngine::MatchesPredicate(const TableInfo& table, const Row& row, const Predicate& predicate) const {
  if (predicate.empty) {
    return true;
  }
  std::string value = ColumnValue(table, row, predicate.column);
  if (predicate.op == "=") {
    return value == predicate.value;
  }
  if (predicate.op == "!=") {
    return value != predicate.value;
  }
  if (predicate.op == ">=") {
    return value >= predicate.value;
  }
  if (predicate.op == "<=") {
    return value <= predicate.value;
  }
  throw MiniDbError("unsupported predicate operator: " + predicate.op);
}

bool ExecutionEngine::MatchesQualifiedPredicate(const TableInfo& left_table, const Row& left_row,
                                                const TableInfo& right_table, const Row& right_row,
                                                const Predicate& predicate) const {
  if (predicate.empty) {
    return true;
  }
  std::string table_prefix;
  std::string column = std::string(predicate.column);
  std::size_t dot = column.find('.');
  if (dot != std::string::npos) {
    table_prefix = column.substr(0, dot);
  }

  if (table_prefix == right_table.name) {
    return MatchesPredicate(right_table, right_row, predicate);
  }
  if (table_prefix == left_table.name || table_prefix.empty()) {
    return MatchesPredicate(left_table, left_row, predicate);
  }
  throw MiniDbError("unknown table qualifier: " + table_prefix);
}

std::string ExecutionEngine::ColumnValue(const TableInfo& table, const Row& row, std::string_view column) const {
  std::size_t index = ColumnIndex(table, column);
  if (index >= row.size()) {
    throw MiniDbError("row is missing column: " + std::string(column));
  }
  return row[index];
}

Row ExecutionEngine::ProjectRow(const TableInfo& table, const Row& row, const std::vector<std::string>& columns) const {
  if (columns.size() == 1 && columns[0] == "*") {
    return row;
  }

  Row projected;
  for (const auto& column : columns) {
    projected.push_back(ColumnValue(table, row, column));
  }
  return projected;
}

Row ExecutionEngine::ProjectJoinedRow(const TableInfo& left_table, const Row& left_row, const TableInfo& right_table,
                                      const Row& right_row, const std::vector<std::string>& columns) const {
  Row projected;
  for (const auto& column : columns) {
    std::size_t dot = column.find('.');
    if (dot == std::string::npos) {
      projected.push_back(ColumnValue(left_table, left_row, column));
      continue;
    }

    std::string table_name = column.substr(0, dot);
    if (table_name == left_table.name) {
      projected.push_back(ColumnValue(left_table, left_row, column));
    } else if (table_name == right_table.name) {
      projected.push_back(ColumnValue(right_table, right_row, column));
    } else {
      throw MiniDbError("unknown table qualifier: " + table_name);
    }
  }
  return projected;
}

std::string ExecutionEngine::UnqualifiedColumn(std::string_view column) const {
  std::string name = Trim(column);
  std::size_t dot = name.find('.');
  if (dot != std::string::npos) {
    return name.substr(dot + 1);
  }
  return name;
}

}  // namespace minidb
