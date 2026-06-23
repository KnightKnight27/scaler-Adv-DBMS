#include "optimizer/stats.h"

#include "catalog/table_heap.h"
#include "common/tuple.h"
#include "storage/heap_file.h"

#include <algorithm>
#include <unordered_set>

namespace minidb {

unordered_map<string, TableStats> StatsCollector::Collect(CatalogManager* catalog) {
  unordered_map<string, TableStats> out;
  if (!catalog)
    return out;
  for (const auto& name : catalog->ListTables()) {
    TableStats s;
    s.tableName = name;
    TableHeap* t = catalog->GetTable(name);
    if (!t)
      continue;
    const Schema& schema = t->GetSchema();
    s.pageCount = t->GetNumPages();
    s.rowCount = t->GetNumTuples();
    HeapFile* hf = t->GetHeap();
    if (!hf)
      continue;
    // Distinct-count estimate per column via hash set.
    vector<unordered_set<int64_t>> ndvs(schema.GetColumnCount());
    int32_t sample = 0;
    for (auto it = hf->Begin(); it != hf->End(); ++it) {
      Tuple tup;
      t->GetTuple(it.GetRid(), &tup);
      for (size_t i = 0; i < tup.GetSize() && i < schema.GetColumnCount(); ++i) {
        Value v = tup.GetValue(i);
        int64_t h = 1469598103934665603LL;
        switch (v.GetTypeId()) {
        case TypeId::INTEGER:
          h ^= (int64_t)v.GetAsInteger();
          break;
        case TypeId::BIGINT:
          h ^= v.GetAsBigInt();
          break;
        case TypeId::BOOLEAN:
          h ^= v.GetAsBoolean() ? 1 : 0;
          break;
        case TypeId::VARCHAR:
          for (char c : v.GetAsVarchar())
            h = h * 31 + c;
          break;
        default:
          break;
        }
        h *= 1099511628211LL;
        ndvs[i].insert(h);
      }
      ++sample;
    }
    for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
      s.distinctCount[schema.GetColumn(i).GetName()] = (int32_t)ndvs[i].size();
      if (s.distinctCount[schema.GetColumn(i).GetName()] == 0)
        s.distinctCount[schema.GetColumn(i).GetName()] = 1;
    }
    out[name] = std::move(s);
  }
  return out;
}

} // namespace minidb
