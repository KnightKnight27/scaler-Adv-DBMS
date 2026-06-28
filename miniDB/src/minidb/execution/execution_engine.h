#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "minidb/buffer/buffer_pool.h"
#include "minidb/index/b_plus_tree.h"
#include "minidb/sql/parser.h"
#include "minidb/storage/heap_table.h"

namespace minidb {

struct QueryResult {
  std::vector<std::string> columns;
  std::vector<Row> rows;
  std::size_t affected_rows{0};
};

class ExecutionEngine {
 public:
  explicit ExecutionEngine(BufferPool& buffer);

  void CreateTable(std::string name, std::vector<Column> columns);
  QueryResult Execute(std::string_view sql);

 private:
  struct TableInfo {
    std::string name;
    std::vector<Column> columns;
    PageId first_page{kInvalidPageId};
    std::size_t primary_key_column{0};
    BPlusTree primary_index;
  };

  QueryResult ExecuteInsert(const InsertStatement& statement);
  QueryResult ExecuteSelect(const SelectStatement& statement);
  QueryResult ExecuteDelete(const DeleteStatement& statement);
  QueryResult ExecuteJoinSelect(const SelectStatement& statement);

  TableInfo& Table(std::string_view name);
  const TableInfo& Table(std::string_view name) const;
  HeapTable Heap(const TableInfo& table);
  std::vector<std::pair<Rid, Row>> ReadRows(TableInfo& table);
  std::size_t ColumnIndex(const TableInfo& table, std::string_view column) const;
  bool MatchesPredicate(const TableInfo& table, const Row& row, const Predicate& predicate) const;
  bool MatchesQualifiedPredicate(const TableInfo& left_table, const Row& left_row, const TableInfo& right_table,
                                 const Row& right_row, const Predicate& predicate) const;
  std::string ColumnValue(const TableInfo& table, const Row& row, std::string_view column) const;
  Row ProjectRow(const TableInfo& table, const Row& row, const std::vector<std::string>& columns) const;
  Row ProjectJoinedRow(const TableInfo& left_table, const Row& left_row, const TableInfo& right_table,
                       const Row& right_row, const std::vector<std::string>& columns) const;
  std::string UnqualifiedColumn(std::string_view column) const;

  BufferPool& buffer_;
  SqlParser parser_;
  std::unordered_map<std::string, TableInfo> tables_;
};

}  // namespace minidb
