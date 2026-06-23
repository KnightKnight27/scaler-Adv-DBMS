#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

using namespace std;

#include "common/Types.hpp"
#include "index/BPlusTree.hpp"
#include "storage/HeapFile.hpp"

namespace minidb {

class Catalog {
public:
    explicit Catalog(PageManager& page_manager);

    bool createTable(const TableDef& def);
    const TableDef* getTable(const string& name) const;
    HeapFile* getHeapFile(const string& name);
    BPlusTree* getPrimaryIndex(const string& name);
    vector<string> listTables() const;

private:
    PageManager& page_manager_;
    unordered_map<string, TableDef> tables_;
    unordered_map<string, unique_ptr<HeapFile>> heap_files_;
    unordered_map<string, unique_ptr<BPlusTree>> indexes_;
};

}  // namespace minidb
