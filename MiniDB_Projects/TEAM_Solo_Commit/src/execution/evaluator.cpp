#include "evaluator.h"

#include <stdexcept>

namespace minidb {

Value Evaluator::Eval(const Expr* e, const Tuple& row, const Schema& schema) {
    switch (e->type) {
        case ExprType::Const:
            return e->val;
        case ExprType::Column: {
            int idx = schema.GetColIdx(e->col_name);
            if (idx < 0) throw std::runtime_error("unknown column: " + e->col_name);
            return row.GetValue(idx);
        }
        case ExprType::Binary: {
            const std::string& op = e->op;
            if (op == "AND") return Value::MakeBool(Eval(e->left.get(), row, schema).AsBool() &&
                                                    Eval(e->right.get(), row, schema).AsBool());
            if (op == "OR")  return Value::MakeBool(Eval(e->left.get(), row, schema).AsBool() ||
                                                    Eval(e->right.get(), row, schema).AsBool());
            Value a = Eval(e->left.get(), row, schema);
            Value b = Eval(e->right.get(), row, schema);
            int c = a.Compare(b);
            if (op == "=")  return Value::MakeBool(c == 0);
            if (op == "!=") return Value::MakeBool(c != 0);
            if (op == "<")  return Value::MakeBool(c < 0);
            if (op == "<=") return Value::MakeBool(c <= 0);
            if (op == ">")  return Value::MakeBool(c > 0);
            if (op == ">=") return Value::MakeBool(c >= 0);
            throw std::runtime_error("unknown operator: " + op);
        }
    }
    throw std::runtime_error("bad expression node");
}

}  // namespace minidb
