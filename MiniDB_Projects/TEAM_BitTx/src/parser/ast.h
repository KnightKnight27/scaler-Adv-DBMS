#pragma once

#include "common/types.h"

#include <memory>
#include <string>
#include <vector>

namespace minidb {

using namespace std;

enum class StmtType { SELECT, INSERT, CREATE, DROP, UPDATE, DELETE };

struct ColumnDefAst {
  string name;
  TypeId type;
  bool nullable = true;
  bool isPrimaryKey = false;
};

struct Expr {
  virtual ~Expr() = default;
  virtual unique_ptr<Expr> Clone() const = 0;
};

struct ColumnRef : Expr {
  string tableName;
  string columnName;
  unique_ptr<Expr> Clone() const override {
    return make_unique<ColumnRef>(*this);
  }
};

struct Literal : Expr {
  Value v;
  unique_ptr<Expr> Clone() const override {
    return make_unique<Literal>(*this);
  }
};

struct BinaryOp : Expr {
  string op;
  unique_ptr<Expr> lhs;
  unique_ptr<Expr> rhs;
  BinaryOp() = default;
  BinaryOp(const BinaryOp& o) : op(o.op) {
    if (o.lhs)
      lhs = o.lhs->Clone();
    if (o.rhs)
      rhs = o.rhs->Clone();
  }
  BinaryOp& operator=(const BinaryOp& o) {
    op = o.op;
    lhs = o.lhs ? o.lhs->Clone() : nullptr;
    rhs = o.rhs ? o.rhs->Clone() : nullptr;
    return *this;
  }
  unique_ptr<Expr> Clone() const override {
    return make_unique<BinaryOp>(*this);
  }
};

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtType GetType() const = 0;
};

struct SelectStmt : Stmt {
  vector<unique_ptr<Expr>> selectList;
  string fromTable;
  unique_ptr<Expr> whereClause;
  vector<string> groupBy;
  unique_ptr<Expr> havingClause;
  int32_t limitCount = -1;
  vector<pair<string, bool>> orderBy;

  StmtType GetType() const override {
    return StmtType::SELECT;
  }
};

struct InsertStmt : Stmt {
  string tableName;
  vector<string> columns;
  vector<Value> values;

  StmtType GetType() const override {
    return StmtType::INSERT;
  }
};

struct CreateTableStmt : Stmt {
  string tableName;
  vector<ColumnDefAst> columns;

  StmtType GetType() const override {
    return StmtType::CREATE;
  }
};

struct DropTableStmt : Stmt {
  string tableName;

  StmtType GetType() const override {
    return StmtType::DROP;
  }
};

struct DeleteStmt : Stmt {
  string tableName;
  unique_ptr<Expr> whereClause;

  StmtType GetType() const override {
    return StmtType::DELETE;
  }
};

struct UpdateStmt : Stmt {
  string tableName;
  string setColumn;
  unique_ptr<Expr> setValue;
  unique_ptr<Expr> whereClause;

  StmtType GetType() const override {
    return StmtType::UPDATE;
  }
};

} // namespace minidb
