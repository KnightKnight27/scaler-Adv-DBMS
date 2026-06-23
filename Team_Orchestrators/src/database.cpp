#include "minidb/database.hpp"

#include "minidb/exec/operators.hpp"
#include "minidb/exec/planner.hpp"
#include "minidb/exec/predicate.hpp"
#include "minidb/serialize.hpp"
#include "minidb/sql/parser.hpp"
#include <set>
#include <stdexcept>

namespace minidb {

namespace {
// Coerces a literal to the target column type, or throws on a type mismatch.
Value coerce(const Value& v, TypeId target) {
  if (v.type() == target) return v;
  if (target == TypeId::Double && v.type() == TypeId::Int)
    return Value(static_cast<double>(v.as_int()));
  if (target == TypeId::Int && v.type() == TypeId::Double)
    return Value(static_cast<int64_t>(v.as_double()));
  throw std::runtime_error("type mismatch: cannot store " +
                           std::string(type_name(v.type())) + " into " +
                           std::string(type_name(target)));
}
}  // namespace

Database::Database(const std::string& base_path)
    : base_path_(base_path), catalog_path_(base_path + ".catalog") {
  catalog_.load(catalog_path_);
  engine_ = std::make_unique<HeapEngine>(catalog_, base_path + ".data");
  wal_ = std::make_unique<WriteAheadLog>(base_path + ".wal");
  txn_ = std::make_unique<TransactionManager>(lock_mgr_, *wal_);
  // Crash recovery: redo committed work, undo in-flight work, then persist the
  // recovered state and start with a clean (truncated) log.
  txn_->recover(*engine_);
  engine_->flush();
  catalog_.save(catalog_path_);
}

Database::~Database() {
  if (skip_shutdown_flush_) return;  // simulated crash
  try {
    flush();
  } catch (...) {
    // best-effort on shutdown
  }
}

void Database::flush() {
  if (engine_) engine_->flush();
  catalog_.save(catalog_path_);
  if (wal_) wal_->truncate();  // heap+catalog are now durable; checkpoint the log
}

void Database::debug_flush_pages() {
  if (engine_) engine_->flush();  // steal dirty pages to disk without a catalog save
}

void Database::debug_crash() { skip_shutdown_flush_ = true; }

QueryResult Database::execute(const std::string& sql) {
  Parser parser(sql);
  Statement stmt = parser.parse();
  switch (stmt.kind) {
    case StatementKind::CreateTable: return run_create(stmt.create);
    case StatementKind::Insert: return run_insert(stmt.insert);
    case StatementKind::Select: return run_select(stmt.select);
    case StatementKind::Delete: return run_delete(stmt.remove);
    case StatementKind::CreateIndex: return run_create_index(stmt.create_index);
    case StatementKind::Analyze: return run_analyze(stmt.analyze);
    case StatementKind::Explain: return run_explain(stmt.explain);
    case StatementKind::Begin: {
      txn_->begin();
      QueryResult r;
      r.message = "BEGIN";
      return r;
    }
    case StatementKind::Commit: {
      txn_->commit();
      QueryResult r;
      r.message = "COMMIT";
      return r;
    }
    case StatementKind::Rollback: {
      txn_->rollback(*engine_);
      QueryResult r;
      r.message = "ROLLBACK";
      return r;
    }
  }
  throw std::runtime_error("internal: unhandled statement");
}

QueryResult Database::run_create(const CreateTableStmt& s) {
  Schema schema(s.columns);
  catalog_.create_table(s.table, schema);
  catalog_.save(catalog_path_);
  QueryResult r;
  r.message = "Table '" + s.table + "' created (" +
              std::to_string(s.columns.size()) + " columns).";
  return r;
}

QueryResult Database::run_create_index(const CreateIndexStmt& s) {
  TableMeta& meta = catalog_.by_name(s.table);
  size_t col = meta.schema.index_of(s.column);
  if (col == Schema::npos)
    throw std::runtime_error("unknown column: " + s.column);
  IndexId id = catalog_.create_index(s.name, meta.id, col);
  catalog_.save(catalog_path_);
  engine_->build_index(id);
  QueryResult r;
  r.message = "Index '" + s.name + "' created on " + s.table + "(" + s.column + ").";
  return r;
}

QueryResult Database::run_insert(const InsertStmt& s) {
  TableMeta& meta = catalog_.by_name(s.table);
  const Schema& schema = meta.schema;
  if (s.values.size() != schema.size())
    throw std::runtime_error("INSERT column count (" + std::to_string(s.values.size()) +
                             ") does not match table (" + std::to_string(schema.size()) + ")");
  Tuple row;
  row.reserve(schema.size());
  for (size_t i = 0; i < schema.size(); ++i)
    row.push_back(coerce(s.values[i], schema.column(i).type));

  bool implicit = !txn_->active();
  if (implicit) txn_->begin();
  RID rid = engine_->insert(meta.id, row);
  txn_->on_insert(meta.id, rid, serialize_tuple(schema, row));
  if (implicit) txn_->commit();

  QueryResult r;
  r.message = "1 row inserted.";
  return r;
}

QueryResult Database::run_select(const SelectStmt& s) {
  Planner planner;
  std::unique_ptr<Operator> root = planner.build(s, catalog_, *engine_);

  QueryResult r;
  r.is_select = true;
  for (const auto& c : root->out_schema().columns()) r.columns.push_back(c.name);

  root->open();
  while (auto t = root->next()) r.rows.push_back(std::move(*t));
  root->close();
  return r;
}

QueryResult Database::run_analyze(const AnalyzeStmt& s) {
  TableMeta& meta = catalog_.by_name(s.table);
  const Schema& schema = meta.schema;
  std::vector<std::set<std::string>> seen(schema.size());
  size_t rows = 0;
  auto iter = engine_->scan(meta.id);
  RID rid;
  Tuple t;
  while (iter->next(rid, t)) {
    ++rows;
    for (size_t i = 0; i < schema.size() && i < t.size(); ++i)
      seen[i].insert(t[i].to_string());
  }
  TableStats& st = catalog_.stats(meta.id);
  st.row_count = rows;
  st.distinct.clear();
  for (size_t i = 0; i < schema.size(); ++i) st.distinct[i] = seen[i].size();

  QueryResult r;
  r.message = "Analyzed '" + s.table + "': " + std::to_string(rows) + " row(s).";
  return r;
}

QueryResult Database::run_explain(const ExplainStmt& s) {
  Planner planner;
  QueryResult r;
  r.message = planner.explain(s.select, catalog_, *engine_);
  return r;
}

QueryResult Database::run_delete(const DeleteStmt& s) {
  TableMeta& meta = catalog_.by_name(s.table);
  const Schema& schema = meta.schema;

  // Collect matching RIDs first, then remove (avoid mutating during scan).
  std::vector<RID> victims;
  auto iter = engine_->scan(meta.id);
  RID rid;
  Tuple t;
  while (iter->next(rid, t)) {
    if (s.where.empty() || eval_where(schema, t, s.where)) victims.push_back(rid);
  }
  iter.reset();

  bool implicit = !txn_->active();
  if (implicit) txn_->begin();
  size_t deleted = 0;
  for (const RID& v : victims) {
    Optional<Tuple> before = engine_->get(meta.id, v);
    std::vector<uint8_t> before_bytes =
        before ? serialize_tuple(schema, *before) : std::vector<uint8_t>{};
    if (engine_->remove(meta.id, v)) {
      txn_->on_delete(meta.id, v, before_bytes);
      ++deleted;
    }
  }
  if (implicit) txn_->commit();

  QueryResult r;
  r.message = std::to_string(deleted) + " row(s) deleted.";
  return r;
}

}  // namespace minidb
