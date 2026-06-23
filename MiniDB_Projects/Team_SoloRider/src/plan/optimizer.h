#pragma once
#include "sql/ast.h"
#include "catalog/catalog.h"
#include "exec/operator.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include <memory>
#include <unordered_map>

namespace minidb {

class Optimizer {
public:
    Optimizer(Catalog* catalog, 
              std::unordered_map<std::string, HeapFile*> heap_files,
              std::unordered_map<std::string, BPlusTree*> indexes)
        : catalog_(catalog), heap_files_(heap_files), indexes_(indexes) {}
    
    std::unique_ptr<Operator> optimize(ASTNode* ast);
    
private:
    Catalog* catalog_;
    std::unordered_map<std::string, HeapFile*> heap_files_;
    std::unordered_map<std::string, BPlusTree*> indexes_;
    
    std::unique_ptr<Operator> create_scan(const std::string& table_name, ASTNode* condition);
    int extract_eq_key(ASTNode* condition, const std::string& indexed_col);
};

} // namespace minidb
