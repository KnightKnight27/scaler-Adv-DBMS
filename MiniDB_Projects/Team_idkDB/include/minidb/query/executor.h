#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include "minidb/index/b_plus_tree.h"
#include "minidb/query/optimizer.h"
#include "minidb/storage/heap_file.h"

namespace minidb {

class Executor {
 public:
  static std::optional<Record> Select(const QueryPlan &plan, HeapFile &heap,
                                      const BPlusTree &index) {
    if (plan.access_path == AccessPath::IndexScan) {
      const auto rid = index.Search(plan.query.key);
      return rid ? heap.Read(*rid) : std::nullopt;
    }
    for (auto &[rid, record] : heap.Scan()) {
      (void)rid;
      if (record.key == plan.query.key) return record;
    }
    return std::nullopt;
  }

  static RID Insert(const Record &record, HeapFile &heap, BPlusTree &index) {
    RID rid = heap.Insert(record);
    if (!index.Insert(record.key, rid)) {
      heap.Delete(rid);
      throw std::runtime_error("duplicate primary key");
    }
    return rid;
  }

  static std::optional<Record> Delete(const QueryPlan &plan, HeapFile &heap,
                                      BPlusTree &index) {
    auto existing = Select(plan, heap, index);
    if (!existing) return std::nullopt;
    auto rid = index.Search(plan.query.key);
    if (!rid) {
      for (auto &[candidate, record] : heap.Scan()) {
        if (record.key == plan.query.key) {
          rid = candidate;
          break;
        }
      }
    }
    if (!rid || !heap.Delete(*rid)) return std::nullopt;
    index.Delete(plan.query.key);
    return existing;
  }
};

}  // namespace minidb
