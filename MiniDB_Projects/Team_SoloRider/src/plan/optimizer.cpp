#include "plan/optimizer.h"
#include "exec/seq_scan.h"
#include "exec/index_scan.h"
#include "exec/filter.h"
#include "exec/projection.h"
#include "exec/nested_loop_join.h"

namespace minidb {

int Optimizer::extract_eq_key(ASTNode* condition, const std::string& indexed_col) {
    if (!condition) return -1;
    if (condition->type == ASTNodeType::BINARY_EXPR) {
        BinaryExpr* bin = static_cast<BinaryExpr*>(condition);
        if (bin->op == "=" && bin->left->type == ASTNodeType::COLUMN_REF && bin->right->type == ASTNodeType::LITERAL) {
            ColumnRef* col = static_cast<ColumnRef*>(bin->left.get());
            Literal* lit = static_cast<Literal*>(bin->right.get());
            if (col->column_name == indexed_col && std::holds_alternative<int>(lit->value)) {
                return std::get<int>(lit->value);
            }
        }
    }
    return -1;
}

std::unique_ptr<Operator> Optimizer::create_scan(const std::string& table_name, ASTNode* condition) {
    TableInfo* info = catalog_->get_table(table_name);
    if (!info) return nullptr;
    
    HeapFile* hf = heap_files_[table_name];
    std::unique_ptr<Operator> scan;
    
    // Scan Choice Rule: If WHERE has equality on PK, and index exists, use IndexScan
    bool used_index = false;
    if (info->has_index && info->primary_key_column != -1) {
        std::string pk_col = info->schema.columns[info->primary_key_column].name;
        int key = extract_eq_key(condition, pk_col);
        if (key != -1 && indexes_.count(table_name)) {
            scan = std::make_unique<IndexScan>(indexes_[table_name], hf, info->schema, key);
            used_index = true;
        }
    }
    
    if (!used_index) {
        scan = std::make_unique<SeqScan>(hf, info->schema);
    }
    
    // Filter
    if (condition) {
        scan = std::make_unique<Filter>(std::move(scan), condition);
    }
    return scan;
}

std::unique_ptr<Operator> Optimizer::optimize(ASTNode* ast) {
    if (ast->type != ASTNodeType::SELECT_STMT) return nullptr;
    
    SelectStmt* sel = static_cast<SelectStmt*>(ast);
    
    // 1. Scan/Filter for from_table
    auto root = create_scan(sel->from_table, sel->where_clause.get());
    if (!root) return nullptr;
    
    // 2. Joins
    for (const auto& join : sel->joins) {
        TableInfo* info1 = catalog_->get_table(sel->from_table);
        TableInfo* info2 = catalog_->get_table(join.table_name);
        
        auto right_scan = create_scan(join.table_name, nullptr);
        
        // Join ordering heuristic: smaller table outer
        if (info1 && info2 && info1->row_count > info2->row_count) {
            // Swap to make info2 outer
            root = std::make_unique<NestedLoopJoin>(std::move(right_scan), std::move(root), join.condition.get());
        } else {
            root = std::make_unique<NestedLoopJoin>(std::move(root), std::move(right_scan), join.condition.get());
        }
    }
    
    // 3. Projection
    std::vector<int> col_indices;
    for (const auto& col_node : sel->columns) {
        if (col_node->type == ASTNodeType::COLUMN_REF) {
            ColumnRef* col = static_cast<ColumnRef*>(col_node.get());
            col_indices.push_back(root->get_schema().find_column(col->column_name));
        } else if (col_node->type == ASTNodeType::STAR_EXPR) {
            for (size_t i = 0; i < root->get_schema().columns.size(); i++) {
                col_indices.push_back(i);
            }
        }
    }
    
    if (!col_indices.empty()) {
        root = std::make_unique<Projection>(std::move(root), col_indices);
    }
    
    return root;
}

} // namespace minidb
