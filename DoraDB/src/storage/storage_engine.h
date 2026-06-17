#pragma once

#include "common/types.h"
#include <string>
#include <vector>

// ============================================================
// StorageEngine — abstract interface for database backends
//
// Both HeapEngine (heap files + B+Tree) and LSMEngine (MemTable
// + SSTables) implement this. The benchmark harness runs the
// same workload through this interface for clean comparison.
// ============================================================

class StorageEngine {
public:
    virtual ~StorageEngine() = default;

    virtual void CreateTable(const std::string& name, const Schema& schema) = 0;

    // Insert a row. Returns true on success.
    virtual bool Insert(const std::string& table, const Row& row) = 0;

    // Full table scan — returns all rows.
    virtual std::vector<Row> Scan(const std::string& table) = 0;

    // Exact key lookup (primary key).
    virtual std::vector<Row> Get(const std::string& table, int key) = 0;

    // Range scan on primary key: returns rows where lowKey <= pk <= highKey.
    // This is what makes IndexScan vs SeqScan a real optimizer choice.
    virtual std::vector<Row> RangeScan(const std::string& table,
                                       int low_key, int high_key) = 0;

    // Delete rows matching the primary key. Returns true if any deleted.
    virtual bool Remove(const std::string& table, int key) = 0;

    // Update the row with the given primary key. Returns true on success.
    virtual bool Update(const std::string& table, int key, const Row& new_row) = 0;
};
