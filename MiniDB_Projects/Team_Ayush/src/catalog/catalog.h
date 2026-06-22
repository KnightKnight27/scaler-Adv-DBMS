#pragma once
#include <string>
#include <vector>
#include "common/config.h"
#include "record/schema.h"

namespace minidb {

// Everything the engine needs to know about one table.
struct TableInfo {
  std::string name;
  Schema      schema;
  PageId      heap_first;        // first page of the heap file
  PageId      pk_index_header;   // B+Tree header page for the PK index, or INVALID
  long        row_count;         // maintained estimate, used by the optimizer

  TableInfo()
      : heap_first(INVALID_PAGE_ID),
        pk_index_header(INVALID_PAGE_ID),
        row_count(0) {}
};

// The system catalog: the set of tables and their metadata. It is serialized
// to/from a single meta page (page 0 of the database file). For an MVP the whole
// catalog is assumed to fit in one page (a handful of small tables).
class Catalog {
 public:
  std::vector<TableInfo> tables;

  TableInfo* Find(const std::string& name) {
    for (auto& t : tables) if (t.name == name) return &t;
    return nullptr;
  }

  // Serialize all tables into `page` (PAGE_SIZE bytes).
  void SerializeTo(char* page) const;
  // Rebuild from a meta page previously written by SerializeTo.
  void DeserializeFrom(const char* page);
};

}  // namespace minidb
