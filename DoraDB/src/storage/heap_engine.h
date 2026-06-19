#pragma once

#include "storage/storage_engine.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "catalog/catalog.h"
#include "optimizer/optimizer.h"
#include "parser/parser.h"

#include <string>
#include <unordered_map>

// ============================================================
// HeapEngine — the main database engine
//
// Implements StorageEngine interface (for benchmarks later).
// Also handles full SQL execution via Execute(Statement).
// Manages heap files, B+Tree indexes, and the catalog.
// ============================================================

class HeapEngine : public StorageEngine {
public:
    explicit HeapEngine(const std::string& data_dir);
    ~HeapEngine() override;

    // ---- StorageEngine interface ----
    void CreateTable(const std::string& name, const Schema& schema) override;
    bool Insert(const std::string& table, const Row& row) override;
    std::vector<Row> Scan(const std::string& table) override;
    std::vector<Row> Get(const std::string& table, int key) override;
    std::vector<Row> RangeScan(const std::string& table, int low_key, int high_key) override;
    bool Remove(const std::string& table, int key) override;
    bool Update(const std::string& table, int key, const Row& new_row) override;

    // ---- SQL execution (called from REPL) ----
    std::string Execute(const Statement& stmt);

    // ---- Access for optimizer/executor ----
    HeapFile* GetHeapFile(const std::string& table);
    BPlusTree* GetIndex(const std::string& table);
    const Schema& GetSchema(const std::string& table) const;
    const TableStats& GetStats(const std::string& table) const;
    Catalog& GetCatalog() { return catalog_; }

private:
    std::string data_dir_;
    DiskManager* disk_mgr_;
    BufferPool* buffer_pool_;
    Catalog catalog_;

    struct TableState {
        HeapFile* heap = nullptr;
        BPlusTree* index = nullptr;
        TableStats stats;
    };
    std::unordered_map<std::string, TableState> tables_;

    // SQL handlers
    std::string ExecCreateTable(const CreateTableStmt& stmt);
    std::string ExecInsert(const InsertStmt& stmt);
    std::string ExecSelect(const SelectStmt& stmt);
    std::string ExecDelete(const DeleteStmt& stmt);
    std::string ExecUpdate(const UpdateStmt& stmt);

    // Load existing tables from catalog on startup
    void LoadTables();

    // Format rows as a table string for display
    std::string FormatResults(const std::vector<Row>& rows, const Schema& schema) const;
};
