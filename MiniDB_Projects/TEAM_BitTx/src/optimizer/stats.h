#pragma once

#include "catalog/catalog.h"
#include "common/types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

// Per-table statistics used by the cost-based optimizer.
struct TableStats {
  string tableName;
  int32_t rowCount = 0;                         // estimated number of rows
  int32_t pageCount = 0;                        // estimated number of pages
  unordered_map<string, int32_t> distinctCount; // per-column NDV estimate
};

class StatsCollector {
public:
  // Build statistics for every table in the catalog by scanning the heap.
  static unordered_map<string, TableStats> Collect(CatalogManager* catalog);
};

} // namespace minidb
