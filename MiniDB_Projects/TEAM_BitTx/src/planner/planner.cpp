#include "planner/planner.h"

#include "executor/evaluator.h"

#include <functional>
#include <stdexcept>

namespace minidb {

using namespace std;

namespace {

function<bool(const Tuple&)> CompilePredicate(const Expr& expr, const Schema& schema) {
  return [&expr, schema](const Tuple& t) {
    Value res = Evaluator::Eval(expr, t.GetValues(), &schema);
    if (res.IsNull())
      return false;
    if (res.GetTypeId() == TypeId::BOOLEAN)
      return res.GetAsBoolean();
    if (res.GetTypeId() == TypeId::INTEGER)
      return res.GetAsInteger() != 0;
    if (res.GetTypeId() == TypeId::BIGINT)
      return res.GetAsBigInt() != 0;
    return false;
  };
}

} // namespace

unique_ptr<AbstractExecutor> Planner::Plan(const Stmt& stmt) {
  switch (stmt.GetType()) {
  case StmtType::SELECT:
    return PlanSelect(static_cast<const SelectStmt&>(stmt));
  case StmtType::INSERT:
    return PlanInsert(static_cast<const InsertStmt&>(stmt));
  case StmtType::DELETE:
    return PlanDelete(static_cast<const DeleteStmt&>(stmt));
  case StmtType::UPDATE:
    return PlanUpdate(static_cast<const UpdateStmt&>(stmt));
  case StmtType::CREATE:
    return PlanCreate(static_cast<const CreateTableStmt&>(stmt));
  case StmtType::DROP:
    return PlanDrop(static_cast<const DropTableStmt&>(stmt));
  }
  return nullptr;
}

unique_ptr<AbstractExecutor> Planner::PlanSelect(const SelectStmt& stmt) {
  TableHeap* t = catalog_->GetTable(stmt.fromTable);
  if (!t)
    throw runtime_error("Table not found: " + stmt.fromTable);
  unique_ptr<AbstractExecutor> exec = make_unique<SeqScanExecutor>(nullptr, t);
  if (stmt.whereClause) {
    exec = make_unique<FilterExecutor>(move(exec), CompilePredicate(*stmt.whereClause, t->GetSchema()), stmt.whereClause->Clone());
  }
  if (!stmt.groupBy.empty()) {
    vector<size_t> keyIdx;
    for (auto& c : stmt.groupBy) {
      keyIdx.push_back(t->GetSchema().GetColumnIndex(c));
    }
    vector<size_t> aggIdx = {0};
    vector<AggregateExecutor::AggType> aggTypes = {AggregateExecutor::AggType::COUNT};
    exec = make_unique<GroupByExecutor>(move(exec), keyIdx, aggIdx, aggTypes);
  }
  if (!stmt.orderBy.empty()) {
    auto& ob = stmt.orderBy[0];
    size_t colIdx = t->GetSchema().GetColumnIndex(ob.first);
    exec = make_unique<SortExecutor>(move(exec), colIdx, ob.second);
  }
  if (stmt.havingClause && !stmt.groupBy.empty()) {
    exec = make_unique<FilterExecutor>(move(exec), CompilePredicate(*stmt.havingClause, exec->GetOutputSchema()), stmt.havingClause->Clone());
  }
  if (stmt.limitCount >= 0) {
    exec = make_unique<LimitExecutor>(move(exec), (size_t)stmt.limitCount);
  }
  return exec;
}

unique_ptr<AbstractExecutor> Planner::PlanInsert(const InsertStmt& stmt) {
  TableHeap* t = catalog_->GetTable(stmt.tableName);
  if (!t)
    throw runtime_error("Table not found: " + stmt.tableName);
  return make_unique<InsertExecutor>(catalog_, stmt.tableName, t, stmt.values);
}

unique_ptr<AbstractExecutor> Planner::PlanDelete(const DeleteStmt& stmt) {
  TableHeap* t = catalog_->GetTable(stmt.tableName);
  if (!t)
    throw runtime_error("Table not found: " + stmt.tableName);
  unique_ptr<AbstractExecutor> child = make_unique<SeqScanExecutor>(nullptr, t);
  if (stmt.whereClause) {
    child = make_unique<FilterExecutor>(move(child), CompilePredicate(*stmt.whereClause, t->GetSchema()), stmt.whereClause->Clone());
  }
  return make_unique<DeleteExecutor>(catalog_, stmt.tableName, t, move(child));
}

unique_ptr<AbstractExecutor> Planner::PlanUpdate(const UpdateStmt& stmt) {
  TableHeap* t = catalog_->GetTable(stmt.tableName);
  if (!t)
    throw runtime_error("Table not found: " + stmt.tableName);
  unique_ptr<AbstractExecutor> child = make_unique<SeqScanExecutor>(nullptr, t);
  if (stmt.whereClause) {
    child = make_unique<FilterExecutor>(move(child), CompilePredicate(*stmt.whereClause, t->GetSchema()), stmt.whereClause->Clone());
  }
  size_t colIdx = t->GetSchema().GetColumnIndex(stmt.setColumn);
  return make_unique<UpdateExecutor>(catalog_, stmt.tableName, t, move(child), colIdx, stmt.setValue->Clone());
}

unique_ptr<AbstractExecutor> Planner::PlanCreate(const CreateTableStmt& stmt) {
  vector<Column> cols;
  for (const auto& c : stmt.columns) {
    cols.push_back(Column(c.name, c.type, c.nullable, c.isPrimaryKey));
  }
  catalog_->CreateTable(stmt.tableName, Schema(cols));
  return nullptr;
}

unique_ptr<AbstractExecutor> Planner::PlanDrop(const DropTableStmt& stmt) {
  catalog_->DropTable(stmt.tableName);
  return nullptr;
}

} // namespace minidb
