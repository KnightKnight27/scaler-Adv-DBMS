#ifndef MINIDB_CATALOG_H
#define MINIDB_CATALOG_H

#include <string>
#include <unordered_map>

// Forward-declare Table so Catalog.h compiles without pulling in
// the entire Table/BPlusTree/BufferPool header chain.
// The full definition is only needed in Catalog.cpp.
class Table;

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
        int numRows     = 0;
        int numDistinct = 1;  // default to 1 to avoid division by zero
    };

    Catalog() = default;

    /** Register a table in the catalog. */
    void addTable(const std::string& name, Table* table);

    /** Look up a table by name. Throws if not found. */
    Table* getTable(const std::string& name);

    /** Check if a table exists. */
    bool hasTable(const std::string& name) const;

    /** Get statistics for a table. */
    TableStats getStats(const std::string& name);

    /** Update statistics (called after bulk inserts). */
    void refreshStats(const std::string& name);

private:
    std::unordered_map<std::string, Table*>      tables_;   // non-owning
    std::unordered_map<std::string, TableStats>  stats_;
};

#endif // MINIDB_CATALOG_H
