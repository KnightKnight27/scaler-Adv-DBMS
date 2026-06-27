#pragma once
#include "catalog/catalog.h"
#include "optimizer/optimizer.h"
#include "storage/heap_file.h"
#include "storage/buffer_pool.h"
#include "index/btree.h"
#include <vector>
#include <unordered_map>
#include <string>

class Executor {
public:
    Executor(Catalog&                                      catalog,
             std::unordered_map<std::string, HeapFile*>&  heap_files,
             std::unordered_map<std::string, BTree*>&     indexes,
             BufferPool&                                   buffer_pool);

    std::vector<Row> execute_select(const QueryPlan& plan,
                                    const SelectStmt& stmt);

    int execute_insert(const std::string& table, const Row& values);

    int execute_delete(const std::string& table, Expression* where);

    std::vector<int> matching_pks(const std::string& table, Expression* where);

    int  recover_insert(const std::string& table, const Row& values);
    void recover_delete(const std::string& table, int pk_val);

    void flush(const std::string& table);

private:
    Catalog&                                     catalog_;
    std::unordered_map<std::string, HeapFile*>&  heap_files_;
    std::unordered_map<std::string, BTree*>&     indexes_;
    BufferPool&                                  pool_;

    std::vector<Row> full_scan(const std::string& table);

    bool eval_filter(Expression* expr, const Row& row, const TableSchema& schema) const;
    std::string eval_value(Expression* expr, const Row& row, const TableSchema& schema) const;

    Row project(const Row& row, const std::vector<std::string>& cols,
                const TableSchema& schema) const;
};
