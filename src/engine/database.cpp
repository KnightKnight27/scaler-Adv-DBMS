#include "engine/database.h"

#include "common/exception.h"

namespace minidb {

Database::Database(const std::string &db_file, size_t pool_size) {
  disk_ = std::make_unique<DiskManager>(db_file);
  bpm_ = std::make_unique<BufferPoolManager>(pool_size, disk_.get());
  catalog_ = std::make_unique<Catalog>(bpm_.get());  // claims page 0
}

Database::~Database() {
  if (bpm_) bpm_->FlushAllPages();
}

ResultSet Database::Execute(const std::string &sql) {
  StmtPtr stmt = Parser::Parse(sql);
  switch (stmt->type) {
    case StmtType::kCreateTable: return ExecCreateTable(stmt.get());
    case StmtType::kInsert: return ExecInsert(stmt.get());
    case StmtType::kDelete: return ExecDelete(stmt.get());
    case StmtType::kSelect: return ExecSelect(stmt.get());
    case StmtType::kCreateIndex:
      throw Exception(ErrorKind::kNotImplemented, "CREATE INDEX arrives with the B+ tree (M2)");
  }
  throw Exception(ErrorKind::kExecution, "unhandled statement");
}

ResultSet Database::ExecCreateTable(Statement *stmt) {
  std::vector<Column> cols;
  for (auto &c : stmt->columns) cols.emplace_back(c.name, c.type, c.length);
  TableMeta *meta = catalog_->CreateTable(stmt->table, Schema(std::move(cols)));
  if (meta == nullptr) throw Exception(ErrorKind::kBinder, "table already exists: " + stmt->table);
  return {false, {}, {}, "CREATE TABLE " + stmt->table, 0};
}

ResultSet Database::ExecInsert(Statement *stmt) {
  TableMeta *meta = catalog_->GetTable(stmt->table);
  if (meta == nullptr) throw Exception(ErrorKind::kBinder, "no such table: " + stmt->table);
  TableHeap *heap = catalog_->GetTableHeap(stmt->table);
  const Schema &schema = meta->schema;

  int count = 0;
  for (auto &row : stmt->rows) {
    if (row.size() != schema.GetColumnCount()) {
      throw Exception(ErrorKind::kBinder, "INSERT column count mismatch for " + stmt->table);
    }
    Tuple t(row, schema);
    RID rid;
    if (!heap->InsertTuple(t, &rid)) throw Exception(ErrorKind::kExecution, "insert failed (row too large?)");
    count++;
  }
  return {false, {}, {}, "INSERT " + std::to_string(count), count};
}

ResultSet Database::ExecDelete(Statement *stmt) {
  TableMeta *meta = catalog_->GetTable(stmt->table);
  if (meta == nullptr) throw Exception(ErrorKind::kBinder, "no such table: " + stmt->table);
  TableHeap *heap = catalog_->GetTableHeap(stmt->table);
  const Schema &schema = meta->schema;

  BoundInput in;
  in.schema = schema;
  in.tables.assign(schema.GetColumnCount(), stmt->table);
  if (stmt->where) BindExpr(stmt->where.get(), in);

  // Collect matching RIDs first, then delete (don't mutate while iterating).
  std::vector<RID> victims;
  for (auto it = heap->Begin(); it != heap->End(); ++it) {
    Tuple t = *it;
    std::vector<Value> vals = t.GetValues(schema);
    if (!stmt->where || EvalPredicate(stmt->where.get(), vals)) victims.push_back(it.GetRID());
  }
  for (const RID &rid : victims) heap->MarkDelete(rid);
  return {false, {}, {}, "DELETE " + std::to_string(victims.size()), static_cast<int>(victims.size())};
}

// ---- SELECT: bind, plan a Volcano pipeline, run it -------------------------
Database::BoundInput Database::BuildInput(Statement *stmt) {
  TableMeta *left = catalog_->GetTable(stmt->from_table);
  if (left == nullptr) throw Exception(ErrorKind::kBinder, "no such table: " + stmt->from_table);

  BoundInput in;
  std::vector<Column> cols = left->schema.GetColumns();
  in.tables.assign(cols.size(), stmt->from_table);

  if (stmt->has_join) {
    TableMeta *right = catalog_->GetTable(stmt->join_table);
    if (right == nullptr) throw Exception(ErrorKind::kBinder, "no such table: " + stmt->join_table);
    for (auto &c : right->schema.GetColumns()) {
      cols.push_back(c);
      in.tables.push_back(stmt->join_table);
    }
  }
  in.schema = Schema(std::move(cols));
  return in;
}

int Database::ResolveColumn(const BoundInput &in, const std::string &table, const std::string &name) {
  int found = -1;
  for (size_t i = 0; i < in.schema.GetColumnCount(); i++) {
    if (in.schema.GetColumn(i).name != name) continue;
    if (!table.empty() && in.tables[i] != table) continue;
    if (found != -1) throw Exception(ErrorKind::kBinder, "ambiguous column: " + name);
    found = static_cast<int>(i);
  }
  if (found == -1) throw Exception(ErrorKind::kBinder, "unknown column: " + (table.empty() ? name : table + "." + name));
  return found;
}

void Database::BindExpr(Expr *e, const BoundInput &in) {
  if (e == nullptr) return;
  if (e->kind == ExprKind::kColumn) {
    e->index = ResolveColumn(in, e->col_table, e->col_name);
  } else if (e->kind == ExprKind::kBinary) {
    BindExpr(e->left.get(), in);
    BindExpr(e->right.get(), in);
  }
}

ResultSet Database::ExecSelect(Statement *stmt) {
  BoundInput in = BuildInput(stmt);

  // Build the scan / join source.
  std::unique_ptr<Executor> exec;
  TableHeap *left_heap = catalog_->GetTableHeap(stmt->from_table);
  const Schema &left_schema = catalog_->GetTable(stmt->from_table)->schema;
  auto left_scan = std::make_unique<SeqScanExecutor>(left_heap, &left_schema);

  if (stmt->has_join) {
    TableHeap *right_heap = catalog_->GetTableHeap(stmt->join_table);
    const Schema &right_schema = catalog_->GetTable(stmt->join_table)->schema;
    auto right_scan = std::make_unique<SeqScanExecutor>(right_heap, &right_schema);
    if (stmt->join_on) BindExpr(stmt->join_on.get(), in);
    exec = std::make_unique<NestedLoopJoinExecutor>(std::move(left_scan), std::move(right_scan),
                                                    stmt->join_on.get());
  } else {
    exec = std::move(left_scan);
  }

  // WHERE filter.
  if (stmt->where) {
    BindExpr(stmt->where.get(), in);
    exec = std::make_unique<FilterExecutor>(std::move(exec), stmt->where.get());
  }

  // Projection + output column names.
  ResultSet rs;
  rs.is_query = true;
  std::vector<int> proj;
  if (stmt->select_star) {
    for (size_t i = 0; i < in.schema.GetColumnCount(); i++) {
      proj.push_back(static_cast<int>(i));
      rs.columns.push_back(in.tables[i] + "." + in.schema.GetColumn(i).name);
    }
  } else {
    for (const std::string &item : stmt->select_list) {
      std::string table, name;
      auto dot = item.find('.');
      if (dot != std::string::npos) {
        table = item.substr(0, dot);
        name = item.substr(dot + 1);
      } else {
        name = item;
      }
      int idx = ResolveColumn(in, table, name);
      proj.push_back(idx);
      rs.columns.push_back(item);
    }
  }
  exec = std::make_unique<ProjectionExecutor>(std::move(exec), proj);

  exec->Init();
  std::vector<Value> row;
  while (exec->Next(&row)) rs.rows.push_back(row);
  rs.affected = static_cast<int>(rs.rows.size());
  return rs;
}

}  // namespace minidb
