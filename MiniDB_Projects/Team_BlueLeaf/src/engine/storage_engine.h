#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "catalog/schema.h"
#include "common/types.h"

namespace minidb {

// Coarse storage statistics, used by the optimizer and the Track-C benchmark
// (space amplification = bytes_on_disk / live data).
struct EngineStats {
    std::uint64_t bytes_on_disk = 0;
    std::uint64_t live_rows     = 0;
};

// The storage abstraction both engines implement. MiniDB tables are keyed by an
// int64 primary key; the value is the encoded row (see Record). The SQL executor
// and optimizer talk ONLY to this interface, so the row-store engine (heap +
// B+Tree) and the LSM engine (M5) are interchangeable behind one factory — which
// is what makes the Track-C comparison apples-to-apples.
class StorageEngine {
public:
    virtual ~StorageEngine() = default;

    // Register a table's schema/layout with the engine (created or reopened).
    virtual void create_table(const std::string& table, const Schema& schema, int pk_col) = 0;

    // Insert (false if the key already exists), point-read, and delete by key.
    virtual bool put(const std::string& table, std::int64_t key, const std::string& row) = 0;
    virtual bool get(const std::string& table, std::int64_t key, std::string& out) = 0;
    virtual bool erase(const std::string& table, std::int64_t key) = 0;

    // Cursor over rows. yields (key, encoded row).
    class Cursor {
    public:
        virtual ~Cursor() = default;
        virtual bool next(std::int64_t& key, std::string& row) = 0;
    };

    // Full table scan (the "table scan" the optimizer may choose) and a bounded
    // key range scan (the "index scan").
    virtual std::unique_ptr<Cursor> scan(const std::string& table) = 0;
    virtual std::unique_ptr<Cursor> range(const std::string& table,
                                          std::int64_t lo, std::int64_t hi) = 0;

    // Force durable state (write-back / WAL flush).
    virtual void flush() = 0;

    virtual EngineStats stats(const std::string& table) = 0;
};

enum class EngineKind { Row, Lsm };

} // namespace minidb
