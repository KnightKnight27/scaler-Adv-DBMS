#ifndef MINIDB_CATALOG_H
#define MINIDB_CATALOG_H

#include <string>
#include <unordered_map>
#include <stdexcept>

#include "Table.h"

/**
 * Catalog is the system catalog (data dictionary) for MiniDB.
 *
 * PURPOSE:
 * In any relational database, the catalog stores metadata about all tables:
 * table names, schemas, statistics for the optimizer, and references to
 * the underlying storage structures. PostgreSQL stores this in pg_class,
 * pg_attribute, pg_statistic. MySQL/InnoDB stores it in the data dictionary
 * tablespace. Our Catalog is a simplified in-memory version.
 *
 * STATISTICS:
 * The catalog tracks per-table statistics used by the Cost-Based Optimizer:
 *   - numRows: total number of live records
 *   - numDistinct: estimated number of distinct values in the id column
 * These are used for selectivity estimation (e.g., equality selectivity
 * = 1/numDistinct). In a production system, these would be computed by
 * an ANALYZE command and stored persistently.
 *
 * OWNERSHIP:
 * The Catalog does NOT own the Table objects — it stores non-owning pointers.
 * The caller is responsible for the Table lifecycle.
 */
class Catalog {
public:
    struct TableStats {
        int numRows    = 0;
        int numDistinct = 1;  // default to 1 to avoid division by zero
    };

    Catalog() = default;

    /** Register a table in the catalog. */
    void addTable(const std::string& name, Table* table) {
        tables_[name] = table;
        // Initialize statistics from the table's current state
        TableStats stats;
        stats.numRows    = table->getNumRows();
        stats.numDistinct = std::max(1, stats.numRows);  // assume all keys distinct
        stats_[name] = stats;
    }

    const std::unordered_map<std::string, Table*>& getTables() const { return tables_; }

    /** Look up a table by name. Throws if not found. */
    Table* getTable(const std::string& name) {
        auto it = tables_.find(name);
        if (it == tables_.end()) {
            throw std::runtime_error("Table not found in catalog: " + name);
        }
        return it->second;
    }

    /** Check if a table exists. */
    bool hasTable(const std::string& name) const {
        return tables_.find(name) != tables_.end();
    }

    /** Get statistics for a table. */
    TableStats getStats(const std::string& name) {
        auto it = stats_.find(name);
        if (it == stats_.end()) {
            throw std::runtime_error("No stats for table: " + name);
        }
        // Refresh numRows from the live table
        it->second.numRows = tables_[name]->getNumRows();
        it->second.numDistinct = std::max(1, it->second.numRows);
        return it->second;
    }

    /** Update statistics (called after bulk inserts). */
    void refreshStats(const std::string& name) {
        Table* t = getTable(name);
        stats_[name].numRows = t->getNumRows();
        stats_[name].numDistinct = std::max(1, stats_[name].numRows);
    }

private:
    std::unordered_map<std::string, Table*> tables_;   // non-owning
    std::unordered_map<std::string, TableStats> stats_;
};

#endif // MINIDB_CATALOG_H
