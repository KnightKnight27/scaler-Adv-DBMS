#pragma once

#include "storage/storage_engine.h"
#include "lsm/memtable.h"
#include "lsm/sstable.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// ============================================================
// LSMEngine — Log-Structured Merge-Tree storage engine
//
// Architecture:
//   Write path: MemTable (in-memory RB-tree) → flush → SSTable (on-disk)
//   Read path: MemTable → SSTables (newest first)
//   Compaction: size-tiered (merge SSTables of similar size)
//
// Implements StorageEngine interface for benchmarking against HeapEngine.
// ============================================================

class LSMEngine : public StorageEngine {
public:
    explicit LSMEngine(const std::string& data_dir);
    ~LSMEngine() override;

    void CreateTable(const std::string& name, const Schema& schema) override;
    bool Insert(const std::string& table, const Row& row) override;
    std::vector<Row> Scan(const std::string& table) override;
    std::vector<Row> Get(const std::string& table, int key) override;
    std::vector<Row> RangeScan(const std::string& table, int low, int high) override;
    bool Remove(const std::string& table, int key) override;
    bool Update(const std::string& table, int key, const Row& new_row) override;

    // Force flush memtable to SSTable
    void FlushMemTable(const std::string& table);

    // Run compaction on a table's SSTables
    void Compact(const std::string& table);

    // Stats
    int GetMemTableSize(const std::string& table) const;
    int GetSSTableCount(const std::string& table) const;

private:
    std::string data_dir_;

    struct TableState {
        Schema schema;
        MemTable memtable;
        std::vector<std::unique_ptr<SSTable>> sstables;  // newest first
        int next_sst_id = 0;
    };
    std::unordered_map<std::string, TableState> tables_;

    void MaybeFlush(const std::string& table);
    std::string SSTablePath(const std::string& table, int id);
};
