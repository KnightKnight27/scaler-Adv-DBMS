#pragma once

#include <memory>
#include <vector>

#include "common.h"
#include "concurrency.h"
#include "optimizer.h"

namespace minidb {

class Database;

using Row = std::vector<Value>;

struct ExecutionContext {
  Database* db;
  Transaction* txn;
};

Value eval_expr(const Expr* e, const Schema& schema, const Row& row);

class Executor {
 public:
  virtual ~Executor() = default;
  virtual const Schema& out_schema() const = 0;
  virtual void open() = 0;
  virtual bool next(Row* out) = 0;
};

std::unique_ptr<Executor> build_executor(const PlanPtr& plan, ExecutionContext ctx);

std::string output_name(const SelectItem& item, int index);

}  // namespace minidb
