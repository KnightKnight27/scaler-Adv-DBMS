#include "exec/executor.h"
#include <stdexcept>
#include "exec/operators.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/tuple.h"

namespace minidb {

std::string value_to_string(const Value& v) {
  if (std::holds_alternative<int64_t>(v)) return std::to_string(std::get<int64_t>(v));
  return std::get<std::string>(v);
}

QueryResult Executor::run(const std::string& sql) {
  Parser parser(Lexer(sql).tokenize());
  Statement stmt = parser.parse();
  bool explain = parser.explain();

  if (auto* s = std::get_if<SelectStmt>(&stmt)) return run_select(*s, explain);
  if (auto* s = std::get_if<InsertStmt>(&stmt)) return run_insert(*s);
  if (auto* s = std::get_if<DeleteStmt>(&stmt)) return run_delete(*s);
  throw std::runtime_error("unsupported statement");
}

QueryResult Executor::run_select(const SelectStmt& stmt, bool explain) {
  OperatorPtr plan = optimizer_.plan(stmt);

  QueryResult result;
  if (explain) {
    result.message = explain_plan(*plan);
    return result;
  }

  result.is_select = true;
  for (const Column& c : plan->out_schema().columns) result.columns.push_back(c.name);

  plan->open();
  Row row;
  while (plan->next(row)) result.rows.push_back(row);
  plan->close();
  return result;
}

QueryResult Executor::run_insert(const InsertStmt& stmt) {
  TableInfo* table = catalog_.get(stmt.table);
  if (!table) throw std::runtime_error("unknown table: " + stmt.table);
  if (stmt.values.size() != table->schema.size())
    throw std::runtime_error("INSERT column count does not match table");

  Row row = stmt.values;
  Key key = std::get<int64_t>(row[table->pk_col]);
  table->storage->insert(key, serialize(row, table->schema));

  QueryResult result;
  result.message = "INSERT 1";
  return result;
}

QueryResult Executor::run_delete(const DeleteStmt& stmt) {
  TableInfo* table = catalog_.get(stmt.table);
  if (!table) throw std::runtime_error("unknown table: " + stmt.table);

  // Scan the table, collect the keys of matching rows, then erase them. (We
  // collect first so we never mutate the table while scanning it.)
  Schema schema = qualified_schema(*table);
  OperatorPtr scan = std::make_unique<SeqScan>(*table->storage, schema, table->name);
  OperatorPtr op = stmt.where ? OperatorPtr(std::make_unique<Filter>(std::move(scan), stmt.where.get()))
                              : std::move(scan);

  std::vector<Key> keys;
  op->open();
  Row row;
  while (op->next(row)) keys.push_back(std::get<int64_t>(row[table->pk_col]));
  op->close();

  for (Key k : keys) table->storage->erase(k);

  QueryResult result;
  result.message = "DELETE " + std::to_string(keys.size());
  return result;
}

}  // namespace minidb
