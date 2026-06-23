#pragma once

#include "index/btree.h"
#include "parser/ast.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

enum class ColumnType { INT, STRING };

struct ColumnDef {
    std::string name;
    ColumnType type = ColumnType::INT;
    bool indexed = false;
};

struct TableDef {
    std::string name;
    std::string primary_key = "id";
    std::vector<ColumnDef> columns;
};

struct Row {
    std::string table;
    std::string row_key;
    std::unordered_map<std::string, Value> values;
};

class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm);

    void RegisterTable(TableDef table_def);
    const TableDef* GetTable(const std::string& name) const;

    std::string MakeRowKey(const std::string& table, int64_t pk) const;
    std::string SerializeRow(const std::string& table,
                             const std::unordered_map<std::string, Value>& values) const;
    Row DeserializeRow(const std::string& table, const std::string& row_key,
                       const std::string& payload) const;

    void TrackRow(const std::string& table, const std::string& row_key);
    void UntrackRow(const std::string& table, const std::string& row_key);
    std::vector<std::string> GetRowKeys(const std::string& table) const;

    bool HasIndex(const std::string& table, const std::string& column) const;
    BTree* GetIndex(const std::string& table, const std::string& column);
    void IndexInsert(const std::string& table, const std::string& column, int64_t key,
                     const RecordId& rid, const std::string& row_key);
    void IndexRemove(const std::string& table, const std::string& column, int64_t key);
    std::string FindRowKeyByRecordId(const RecordId& rid) const;

    void RebuildFromTableHeap(const TableHeap& heap);

    int64_t EstimateTableCardinality(const std::string& table) const;
    double EstimateSelectivity(const std::string& column, const std::string& op,
                               int64_t bound, int64_t table_size) const;

    static TableDef DefaultUsersTable();

private:
    std::string IndexKey(const std::string& table, const std::string& column) const;
    std::string RidKey(const RecordId& rid) const;
    page_id_t NextMetaPageId();

    BufferPoolManager* bpm_;
    page_id_t next_meta_page_id_;
    std::unordered_map<std::string, TableDef> tables_;
    std::unordered_map<std::string, std::vector<std::string>> table_row_keys_;
    std::unordered_map<std::string, std::unique_ptr<BTree>> indexes_;
    std::unordered_map<std::string, std::string> rid_to_row_key_;
};

}  // namespace minidb
