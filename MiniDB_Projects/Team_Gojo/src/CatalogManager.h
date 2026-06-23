#ifndef MINIDB_CATALOG_MANAGER_H
#define MINIDB_CATALOG_MANAGER_H

#include <string>

// Forward declarations
class Optimizer;

/**
 * CatalogManager — persistent catalog serialization for MiniDB.
 *
 * Saves and restores the full database state (schemas + heap data) to/from
 * a human-readable text file so that data survives process restarts.
 *
 * FILE FORMAT (catalog.txt):
 *
 *   <num_tables>
 *   For each table:
 *     <table_name>
 *     <db_file_path>
 *     <num_columns>
 *     For each column:
 *       <col_name> <col_type_int>
 *     <num_rows>
 *     <root_page_id>
 *     For each row (num_rows total):
 *       <num_values> <val1_type> <val1> <val2_type> <val2> ...
 *       (type: 0 = INT, 1 = VARCHAR)
 */
class CatalogManager {
public:
    explicit CatalogManager(const std::string& catalogFile = "catalog.txt");

    /**
     * Serialize all tables registered in the optimizer's catalog to disk.
     * Overwrites the catalog file atomically via a .tmp write-then-rename.
     */
    void saveState(const Optimizer& opt) const;

    /**
     * Deserialize the catalog file and reconstruct all tables inside the
     * optimizer's catalog. Clears any existing tables first.
     * No-op if the catalog file does not exist yet.
     */
    void loadState(Optimizer& opt) const;

private:
    std::string catalogFile_;
};

#endif // MINIDB_CATALOG_MANAGER_H
