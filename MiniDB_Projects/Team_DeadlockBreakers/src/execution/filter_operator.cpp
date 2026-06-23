#include "execution/operators.h"
#include <algorithm>

namespace minidb {

FilterOperator::FilterOperator(std::unique_ptr<Operator> child,
                               const WhereClause& clause, const Schema& schema)
    : child_(std::move(child)), where_clause_(clause), schema_(schema) {}

bool FilterOperator::EvalPredicate(const Predicate& pred, const Row& row,
                                   const Schema& schema) {
    bool qualified = pred.column.find('.') != std::string::npos;
    for (size_t index = 0; index < schema.size(); ++index) {
        std::string name = schema[index].name;
        if (qualified && name == pred.column)
            return CompareValues(row[index], pred.value, pred.op);
        size_t dot = name.find('.');
        if (dot != std::string::npos) name = name.substr(dot + 1);
        if (!qualified && name == pred.column)
            return CompareValues(row[index], pred.value, pred.op);
    }
    return false;
}

bool FilterOperator::EvalWhereClause(const WhereClause& clause, const Row& row,
                                     const Schema& schema) {
    if (clause.IsEmpty()) return true;
    for (const auto& pred : clause.preds) {
        bool match = EvalPredicate(pred, row, schema);
        if (clause.logic == LogicOp::AND && !match) return false;
        if (clause.logic == LogicOp::OR && match) return true;
    }
    return clause.logic == LogicOp::AND;
}

void FilterOperator::Open() { child_->Open(); }

TupleBatch FilterOperator::Next() {
    for (;;) {
        TupleBatch input = child_->Next();
        if (input.IsEmpty()) return input;
        input.rows.erase(std::remove_if(input.rows.begin(), input.rows.end(),
            [&](const Row& row) {
                return !EvalWhereClause(where_clause_, row, schema_);
            }), input.rows.end());
        if (!input.IsEmpty()) return input;
    }
}

void FilterOperator::Close() { child_->Close(); }
Schema FilterOperator::GetOutputSchema() const { return schema_; }

} // namespace minidb
