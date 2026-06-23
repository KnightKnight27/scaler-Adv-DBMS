#include "planner/planner.h"

#include "catalog/catalog.h"
#include "optimizer/optimizer.h"
#include "execution/seq_scan_executor.h"
#include "execution/filter_executor.h"
#include "execution/projection_executor.h"
#include "execution/join_executor.h"
#include "execution/values_executor.h"
#include "execution/insert_executor.h"
#include "execution/delete_executor.h"
#include "common/logger.h"

namespace minidb {

namespace {

// Builds a name->index resolver over an ordered list of output columns. A
// reference matches a column when the names are equal and either the reference
// is unqualified or its qualifier matches the column's source table. The first
// match wins (so unqualified references in a join prefer the left table).
ColumnResolver MakeResolver(const std::vector<BoundColumn>& columns) {
    return [columns](const std::string& table, const std::string& name) -> int {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].name == name && (table.empty() || columns[i].table == table)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
}

// The flat output-column layout of a single table scan.
std::vector<BoundColumn> ColumnsForTable(const std::string& table_name, const Schema* schema) {
    std::vector<BoundColumn> cols;
    for (const auto& col : schema->GetColumns()) {
        cols.push_back({table_name, col.name_});
    }
    return cols;
}

// Does a JOIN key reference the given table? Qualified keys match by qualifier;
// unqualified keys match if the column exists in that table's schema.
bool KeyMatchesTable(const SelectColumn& key, const std::string& table_name, const Schema* schema) {
    if (!key.table.empty()) return key.table == table_name;
    try {
        schema->GetColIndex(key.column);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Detects the index-friendly shape `<column> = <constant>` (in either operand
// order) where the column belongs to `schema` (qualifier, if any, must match
// `alias`). On success fills the column index and the literal value.
bool ExtractColumnEquality(const Expression* expr, const Schema* schema,
                           const std::string& alias, uint32_t* out_col, std::string* out_value) {
    const auto* cmp = dynamic_cast<const ComparisonExpression*>(expr);
    if (cmp == nullptr || cmp->op() != CompareOp::EQ) return false;

    const ColumnRefExpression* col = nullptr;
    const ConstantExpression* val = nullptr;

    if ((col = dynamic_cast<const ColumnRefExpression*>(cmp->left())) &&
        (val = dynamic_cast<const ConstantExpression*>(cmp->right()))) {
        // column = constant
    } else if ((col = dynamic_cast<const ColumnRefExpression*>(cmp->right())) &&
               (val = dynamic_cast<const ConstantExpression*>(cmp->left()))) {
        // constant = column
    } else {
        return false;
    }

    if (!col->table().empty() && col->table() != alias) return false;
    try {
        *out_col = schema->GetColIndex(col->column());
    } catch (const std::exception&) {
        return false;
    }
    *out_value = val->value();
    return true;
}

} // namespace

size_t Planner::EstimateTableSize(const TableMetadata* table) const {
    size_t n = 0;
    for (const auto& [key, value] : table->memtable->GetAllEntries()) {
        if (key.size() >= sizeof(table_oid_t) &&
            *reinterpret_cast<const table_oid_t*>(key.data()) == table->oid) {
            ++n;
        }
    }
    // Cheap heuristic for on-disk rows: avoid scanning SSTables during planning.
    n += table->sstable_paths.size() * 1000;
    return n < 2 ? 2 : n;
}

std::unique_ptr<AbstractExecutor> Planner::BuildSingleTableSource(
    const std::string& table_name, const Expression* where) {
    Catalog* catalog = context_->GetCatalog();
    TableMetadata* table = catalog->GetTable(table_name);
    if (table == nullptr) {
        throw PlanError("Table '" + table_name + "' does not exist.");
    }
    const Schema* schema = table->schema.get();

    // Index-aware path: a single equality on an indexed column is handed to the
    // cost-based Optimizer, which decides between an IndexScan and SeqScan+Filter.
    if (where != nullptr) {
        uint32_t col_idx = 0;
        std::string value;
        if (ExtractColumnEquality(where, schema, table_name, &col_idx, &value)) {
            IndexMetadata* index = catalog->GetIndex(table->oid, col_idx);
            BPlusTree* tree = index ? index->tree.get() : nullptr;
            Optimizer optimizer(context_);
            return optimizer.OptimizePointLookup(table->oid, value, col_idx, tree,
                                                 EstimateTableSize(table));
        }
    }

    // General path: full scan, optionally filtered by the rich predicate.
    auto scan = std::make_unique<SeqScanExecutor>(context_, table->oid);
    if (where == nullptr) {
        return scan;
    }

    ExprPtr predicate = where->Clone();
    predicate->Bind(MakeResolver(ColumnsForTable(table_name, schema)));
    return std::make_unique<FilterExecutor>(context_, std::move(scan), std::move(predicate));
}

std::unique_ptr<AbstractExecutor> Planner::BuildJoin(
    const SelectStatement& stmt, std::vector<BoundColumn>* out_columns) {
    Catalog* catalog = context_->GetCatalog();
    TableMetadata* left = catalog->GetTable(stmt.table_name);
    TableMetadata* right = catalog->GetTable(stmt.join.right_table);
    if (left == nullptr) throw PlanError("Table '" + stmt.table_name + "' does not exist.");
    if (right == nullptr) throw PlanError("Table '" + stmt.join.right_table + "' does not exist.");

    const Schema* left_schema = left->schema.get();
    const Schema* right_schema = right->schema.get();

    // Figure out which join key belongs to which side (the user may write the
    // condition in either order).
    const SelectColumn& k1 = stmt.join.left_key;
    const SelectColumn& k2 = stmt.join.right_key;
    SelectColumn left_key, right_key;
    if (KeyMatchesTable(k1, stmt.table_name, left_schema) &&
        KeyMatchesTable(k2, stmt.join.right_table, right_schema)) {
        left_key = k1;
        right_key = k2;
    } else if (KeyMatchesTable(k1, stmt.join.right_table, right_schema) &&
               KeyMatchesTable(k2, stmt.table_name, left_schema)) {
        left_key = k2;
        right_key = k1;
    } else {
        throw PlanError("JOIN condition columns do not match the joined tables.");
    }

    uint32_t left_idx, right_idx;
    try {
        left_idx = left_schema->GetColIndex(left_key.column);
        right_idx = right_schema->GetColIndex(right_key.column);
    } catch (const std::exception& e) {
        throw PlanError(std::string("Invalid JOIN column: ") + e.what());
    }

    // Combined output layout: left columns followed by right columns. This must
    // match NestedLoopJoinExecutor's row concatenation order.
    out_columns->clear();
    for (const auto& c : ColumnsForTable(stmt.table_name, left_schema)) out_columns->push_back(c);
    for (const auto& c : ColumnsForTable(stmt.join.right_table, right_schema)) out_columns->push_back(c);

    auto left_scan = std::make_unique<SeqScanExecutor>(context_, left->oid);
    auto right_scan = std::make_unique<SeqScanExecutor>(context_, right->oid);
    return std::make_unique<NestedLoopJoinExecutor>(
        context_, std::move(left_scan), std::move(right_scan), left_idx, right_idx);
}

std::unique_ptr<AbstractExecutor> Planner::PlanSelect(const SelectStatement& stmt) {
    std::unique_ptr<AbstractExecutor> root;
    std::vector<BoundColumn> output_columns;

    if (stmt.join.present) {
        root = BuildJoin(stmt, &output_columns);
        // Apply the WHERE predicate on top of the join (no index path here).
        if (stmt.where) {
            ExprPtr predicate = stmt.where->Clone();
            predicate->Bind(MakeResolver(output_columns));
            root = std::make_unique<FilterExecutor>(context_, std::move(root), std::move(predicate));
        }
    } else {
        Catalog* catalog = context_->GetCatalog();
        TableMetadata* table = catalog->GetTable(stmt.table_name);
        if (table == nullptr) throw PlanError("Table '" + stmt.table_name + "' does not exist.");
        output_columns = ColumnsForTable(stmt.table_name, table->schema.get());
        root = BuildSingleTableSource(stmt.table_name, stmt.where.get());
    }

    // Projection: resolve each selected column against the source layout.
    if (!stmt.select_star) {
        ColumnResolver resolver = MakeResolver(output_columns);
        std::vector<uint32_t> indices;
        for (const auto& sel : stmt.select_list) {
            int idx = resolver(sel.table, sel.column);
            if (idx < 0) {
                std::string ref = sel.table.empty() ? sel.column : sel.table + "." + sel.column;
                throw PlanError("Unknown column in SELECT list: '" + ref + "'.");
            }
            indices.push_back(static_cast<uint32_t>(idx));
        }
        root = std::make_unique<ProjectionExecutor>(context_, std::move(root), std::move(indices));
    }

    return root;
}

std::unique_ptr<AbstractExecutor> Planner::PlanDelete(const DeleteStatement& stmt) {
    Catalog* catalog = context_->GetCatalog();
    TableMetadata* table = catalog->GetTable(stmt.table_name);
    if (table == nullptr) throw PlanError("Table '" + stmt.table_name + "' does not exist.");

    // The DeleteExecutor needs full rows (PK + indexed columns), so the source
    // is always a SeqScan; a predicate becomes an expression filter on top.
    std::unique_ptr<AbstractExecutor> source = std::make_unique<SeqScanExecutor>(context_, table->oid);
    if (stmt.where) {
        ExprPtr predicate = stmt.where->Clone();
        predicate->Bind(MakeResolver(ColumnsForTable(stmt.table_name, table->schema.get())));
        source = std::make_unique<FilterExecutor>(context_, std::move(source), std::move(predicate));
    }
    return std::make_unique<DeleteExecutor>(context_, std::move(source), table->oid);
}

std::unique_ptr<AbstractExecutor> Planner::PlanInsert(const InsertStatement& stmt) {
    Catalog* catalog = context_->GetCatalog();
    TableMetadata* table = catalog->GetTable(stmt.table_name);
    if (table == nullptr) throw PlanError("Table '" + stmt.table_name + "' does not exist.");

    uint32_t expected = table->schema->GetColumnCount();
    std::vector<Row> rows;
    rows.reserve(stmt.rows.size());
    for (const auto& tuple : stmt.rows) {
        if (tuple.size() != expected) {
            throw PlanError("Table '" + stmt.table_name + "' expects " + std::to_string(expected) +
                            " values, but " + std::to_string(tuple.size()) + " were provided.");
        }
        rows.push_back(Row{tuple});
    }

    auto values = std::make_unique<ValuesExecutor>(context_, std::move(rows));
    return std::make_unique<InsertExecutor>(context_, std::move(values), table->oid);
}

} // namespace minidb
