#pragma once

#include "catalog/catalog.h"
#include "execution/executor.h"
#include "parser/ast.h"

#include <memory>

namespace minidb {

using namespace std;

class Planner {
public:
  explicit Planner(CatalogManager* catalog) : catalog_(catalog) {}

  unique_ptr<AbstractExecutor> Plan(const Stmt& stmt);

private:
  unique_ptr<AbstractExecutor> PlanSelect(const SelectStmt& stmt);
  unique_ptr<AbstractExecutor> PlanInsert(const InsertStmt& stmt);
  unique_ptr<AbstractExecutor> PlanDelete(const DeleteStmt& stmt);
  unique_ptr<AbstractExecutor> PlanUpdate(const UpdateStmt& stmt);
  unique_ptr<AbstractExecutor> PlanCreate(const CreateTableStmt& stmt);
  unique_ptr<AbstractExecutor> PlanDrop(const DropTableStmt& stmt);

  CatalogManager* catalog_;
};

} // namespace minidb