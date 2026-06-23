#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "index/btree.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "storage/page_manager.h"

namespace minidb {

struct TableSchema {
    std::string name;
    std::vector<ColumnDef> columns;
    int heap_first_page = INVALID_PAGE_ID;
    int pk_index_root = INVALID_PAGE_ID;
    std::unordered_map<std::string, int> secondary_indexes;

    int ColumnIndex(const std::string& col) const;
    const ColumnDef* PrimaryKeyColumn() const;
    bool IsColumnIndexed(const std::string& col) const;
};

class Catalog {
public:
    bool CreateTable(const TableSchema& schema);
    std::optional<TableSchema> GetTable(const std::string& name) const;
    void RegisterHeapFile(const std::string& table, std::unique_ptr<HeapFile> heap);
    void RegisterIndex(const std::string& table, const std::string& col, std::unique_ptr<BPlusTree> index);
    HeapFile* GetHeapFile(const std::string& table) const;
    BPlusTree* GetPrimaryIndex(const std::string& table) const;
    BPlusTree* GetSecondaryIndex(const std::string& table, const std::string& col) const;
    const std::unordered_map<std::string, TableSchema>& tables() const { return tables_; }

    void Save(const std::string& filepath) const;
    bool Load(const std::string& filepath, PageManager* pm, BufferPool* bp);

private:
    std::unordered_map<std::string, TableSchema> tables_;
    std::unordered_map<std::string, std::unique_ptr<HeapFile>> heaps_;
    std::unordered_map<std::string, std::unique_ptr<BPlusTree>> pk_indexes_;
    std::unordered_map<std::string, std::unique_ptr<BPlusTree>> secondary_indexes_;
};

}  // namespace minidb
