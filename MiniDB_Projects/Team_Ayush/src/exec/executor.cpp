#include "exec/executor.h"

#include <memory>
#include "index/bplus_tree.h"
#include "optimizer/optimizer.h"
#include "record/tuple.h"
#include "sql/parser.h"
#include "storage/heap_file.h"

namespace minidb {

namespace {

// Resolve a (table, column) reference against an operator's qualified output
// columns ("table.column"). Returns the column index or -1 (or -2 if ambiguous).
int ResolveCol(const std::vector<std::string>& cols, const std::string& table,
               const std::string& col) {
  if (!table.empty()) {
    std::string want = table + "." + col;
    for (size_t i = 0; i < cols.size(); ++i)
      if (cols[i] == want) return static_cast<int>(i);
    return -1;
  }
  // Unqualified: match the suffix ".col", require uniqueness.
  std::string suffix = "." + col;
  int found = -1;
  for (size_t i = 0; i < cols.size(); ++i) {
    const std::string& c = cols[i];
    if (c.size() >= suffix.size() &&
        c.compare(c.size() - suffix.size(), suffix.size(), suffix) == 0) {
      if (found >= 0) return -2;  // ambiguous
      found = static_cast<int>(i);
    }
  }
  return found;
}

ExecResult Err(const std::string& m) {
  ExecResult r; r.ok = false; r.error = m; return r;
}

// Compile WHERE predicates against an operator's output columns.
bool CompilePreds(const std::vector<std::string>& cols,
                  const std::vector<Predicate>& preds,
                  std::vector<CompiledPred>* out, std::string* err) {
  for (const Predicate& p : preds) {
    int idx = ResolveCol(cols, p.table, p.column);
    if (idx == -2) { *err = "ambiguous column: " + p.column; return false; }
    if (idx < 0) { *err = "unknown column: " + p.column; return false; }
    out->push_back({idx, p.op, p.value});
  }
  return true;
}

// ---- single-table plan -----------------------------------------------------
std::unique_ptr<Operator> BuildScan(Database& db, const TableInfo* t,
                                    const std::vector<Predicate>& preds,
                                    std::string* plan, std::string* err) {
  AccessChoice ac = Optimizer::ChooseAccess(*t, preds);
  std::unique_ptr<Operator> base;
  if (ac.use_index) {
    base.reset(new IndexScan(db.pool(), t, ac.low, ac.high));
  } else {
    base.reset(new SeqScan(db.pool(), t));
  }
  if (plan) *plan += "  -> " + ac.reason + "\n";

  std::vector<CompiledPred> cps;
  if (!CompilePreds(base->OutCols(), preds, &cps, err)) return nullptr;
  if (!cps.empty()) {
    base.reset(new Filter(std::move(base), std::move(cps)));
    if (plan) *plan += "  -> Filter (" + std::to_string(preds.size()) +
                       " predicate(s))\n";
  }
  return base;
}

ExecResult DoCreate(Database& db, const CreateTableStmt& c) {
  Status s = db.CreateTable(c);
  if (!s.ok()) return Err(s.message());
  ExecResult r; r.message = "Table '" + c.table + "' created."; return r;
}

ExecResult DoInsert(Database& db, const InsertStmt& ins) {
  TableInfo* t = db.GetTable(ins.table);
  if (!t) return Err("no such table: " + ins.table);
  if (ins.values.size() != t->schema.columns.size())
    return Err("column count mismatch");

  // Coerce literals to the column types where trivially possible.
  std::vector<Value> row = ins.values;
  for (size_t i = 0; i < row.size(); ++i) {
    if (t->schema.columns[i].type != row[i].type)
      return Err("type mismatch for column " + t->schema.columns[i].name);
  }

  std::vector<char> buf;
  tuple::Serialize(t->schema, row, buf);
  HeapFile heap(db.pool(), t->heap_first, t->schema.RecordSize());
  RID rid = heap.Insert(buf.data());

  if (t->pk_index_header != INVALID_PAGE_ID) {
    int pk = t->schema.pk_index;
    BPlusTree tree(db.pool(), t->pk_index_header);
    RID existing;
    if (tree.Search(row[pk].i, &existing))
      return Err("duplicate primary key");
    tree.Insert(row[pk].i, rid);
  }
  t->row_count++;
  ExecResult r; r.message = "1 row inserted."; return r;
}

ExecResult DoDelete(Database& db, const DeleteStmt& d) {
  TableInfo* t = db.GetTable(d.table);
  if (!t) return Err("no such table: " + d.table);

  std::vector<CompiledPred> cps;
  std::string err;
  std::vector<std::string> cols;
  for (const Column& c : t->schema.columns) cols.push_back(t->name + "." + c.name);
  if (!CompilePreds(cols, d.where, &cps, &err)) return Err(err);

  HeapFile heap(db.pool(), t->heap_first, t->schema.RecordSize());
  BPlusTree tree(db.pool(), t->pk_index_header);
  bool has_index = (t->pk_index_header != INVALID_PAGE_ID);
  int pk = t->schema.pk_index;

  std::vector<RID> victims;
  std::vector<int32_t> victim_keys;
  heap.Scan([&](RID rid, const char* data) {
    std::vector<Value> vals = tuple::Deserialize(t->schema, data);
    bool match = true;
    for (const CompiledPred& p : cps) {
      const Value& lhs = vals[p.col_index];
      int cmp = lhs.Compare(p.value);
      bool ok = (p.op == CompOp::EQ && cmp == 0) ||
                (p.op == CompOp::NE && cmp != 0) ||
                (p.op == CompOp::LT && cmp < 0) ||
                (p.op == CompOp::LE && cmp <= 0) ||
                (p.op == CompOp::GT && cmp > 0) ||
                (p.op == CompOp::GE && cmp >= 0);
      if (!ok) { match = false; break; }
    }
    if (match) {
      victims.push_back(rid);
      if (has_index) victim_keys.push_back(vals[pk].i);
    }
  });

  for (RID rid : victims) heap.Delete(rid);
  if (has_index) for (int32_t k : victim_keys) tree.Delete(k);
  t->row_count -= static_cast<long>(victims.size());
  if (t->row_count < 0) t->row_count = 0;

  ExecResult r;
  r.message = std::to_string(victims.size()) + " row(s) deleted.";
  return r;
}

// Split WHERE predicates between two joined tables.
void SplitPreds(const TableInfo* a, const TableInfo* b,
                const std::vector<Predicate>& where,
                std::vector<Predicate>* pa, std::vector<Predicate>* pb) {
  for (const Predicate& p : where) {
    if (p.table == a->name) { pa->push_back(p); continue; }
    if (p.table == b->name) { pb->push_back(p); continue; }
    // Unqualified: assign to whichever table has the column.
    if (a->schema.ColumnIndex(p.column) >= 0) pa->push_back(p);
    else pb->push_back(p);
  }
}

ExecResult DoSelect(Database& db, const SelectStmt& s) {
  TableInfo* from = db.GetTable(s.from_table);
  if (!from) return Err("no such table: " + s.from_table);

  std::string plan;
  std::unique_ptr<Operator> root;

  if (!s.join.present) {
    plan = "Plan for SELECT on '" + from->name + "':\n";
    std::string err;
    root = BuildScan(db, from, s.where, &plan, &err);
    if (!root) return Err(err);
  } else {
    TableInfo* other = db.GetTable(s.join.table);
    if (!other) return Err("no such table: " + s.join.table);

    // Identify each side's join column from the ON clause.
    std::string a_col, b_col;
    if (s.join.left_table == from->name) { a_col = s.join.left_col; b_col = s.join.right_col; }
    else { a_col = s.join.right_col; b_col = s.join.left_col; }

    std::vector<Predicate> preds_from, preds_other;
    SplitPreds(from, other, s.where, &preds_from, &preds_other);

    // Cost-based join order: drive the loop with the smaller relation.
    bool from_is_outer = from->row_count <= other->row_count;
    TableInfo* outer = from_is_outer ? from : other;
    TableInfo* inner = from_is_outer ? other : from;
    std::string outer_join_col = from_is_outer ? a_col : b_col;
    std::string inner_join_col = from_is_outer ? b_col : a_col;
    std::vector<Predicate>& outer_preds = from_is_outer ? preds_from : preds_other;
    std::vector<Predicate>& inner_preds = from_is_outer ? preds_other : preds_from;

    plan = "Plan for JOIN (outer='" + outer->name + "', inner='" + inner->name + "'):\n";
    plan += " Outer " + outer->name + ":\n";
    std::string err;
    std::unique_ptr<Operator> outer_op = BuildScan(db, outer, outer_preds, &plan, &err);
    if (!outer_op) return Err(err);

    int outer_key_idx = ResolveCol(outer_op->OutCols(), outer->name, outer_join_col);
    if (outer_key_idx < 0) return Err("join column not found: " + outer_join_col);

    int inner_join_idx = inner->schema.ColumnIndex(inner_join_col);
    if (inner_join_idx < 0) return Err("join column not found: " + inner_join_col);
    bool inner_indexed =
        inner->schema.pk_index == inner_join_idx &&
        inner->pk_index_header != INVALID_PAGE_ID;

    // Pre-compile inner-only predicates against the inner schema.
    std::vector<std::string> inner_cols;
    for (const Column& c : inner->schema.columns)
      inner_cols.push_back(inner->name + "." + c.name);
    std::vector<CompiledPred> inner_cps;
    if (!CompilePreds(inner_cols, inner_preds, &inner_cps, &err)) return Err(err);

    plan += " Inner " + inner->name + ": " +
            (inner_indexed ? "IndexScan per outer key (index-nested-loop)"
                           : "SeqScan + Filter per outer key (nested-loop)") + "\n";

    BufferPool* pool = db.pool();
    auto factory = [pool, inner, inner_join_idx, inner_indexed,
                    inner_cps](const Value& key) -> std::unique_ptr<Operator> {
      std::unique_ptr<Operator> op;
      std::vector<CompiledPred> preds = inner_cps;
      if (inner_indexed) {
        op.reset(new IndexScan(pool, inner, key.i, key.i));
      } else {
        op.reset(new SeqScan(pool, inner));
        preds.push_back({inner_join_idx, CompOp::EQ, key});
      }
      if (!preds.empty()) op.reset(new Filter(std::move(op), std::move(preds)));
      return op;
    };

    root.reset(new NestedLoopJoin(std::move(outer_op), outer_key_idx,
                                  factory, inner_cols));
  }

  // Projection.
  if (!s.star) {
    std::vector<int> indices;
    std::vector<std::string> names;
    for (const std::string& sc : s.select_list) {
      std::string tbl, col;
      size_t dot = sc.find('.');
      if (dot == std::string::npos) { col = sc; }
      else { tbl = sc.substr(0, dot); col = sc.substr(dot + 1); }
      int idx = ResolveCol(root->OutCols(), tbl, col);
      if (idx == -2) return Err("ambiguous column: " + sc);
      if (idx < 0) return Err("unknown column: " + sc);
      indices.push_back(idx);
      names.push_back(root->OutCols()[idx]);
    }
    root.reset(new Project(std::move(root), indices, names));
    plan += " -> Project " + std::to_string(indices.size()) + " column(s)\n";
  }

  ExecResult r;
  r.is_query = true;
  r.columns = root->OutCols();

  if (s.explain) {
    r.is_explain = true;
    r.plan = plan;
    return r;
  }

  root->Open();
  Row row;
  while (root->Next(row)) r.rows.push_back(row);
  root->Close();
  return r;
}

}  // namespace

ExecResult Execute(Database& db, const std::string& sql) {
  ParseResult pr = Parse(sql);
  if (!pr.ok) return Err("parse error: " + pr.error);

  switch (pr.stmt.type) {
    case StmtType::CREATE_TABLE: return DoCreate(db, pr.stmt.create);
    case StmtType::INSERT:       return DoInsert(db, pr.stmt.insert);
    case StmtType::DELETE_:      return DoDelete(db, pr.stmt.del);
    case StmtType::SELECT:       return DoSelect(db, pr.stmt.select);
    default:                     return Err("unsupported statement");
  }
}

}  // namespace minidb
