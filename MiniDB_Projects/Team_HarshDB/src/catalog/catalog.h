#pragma once
// ---------------------------------------------------------------------------
// catalog.h - MiniDB's system catalog (its "pg_class").
//
// The catalog remembers every table: its schema, where its heap starts on disk,
// which column is the primary key, and the in-memory B+ tree index built on that
// key. Table definitions are persisted to a side metadata file so the database
// can be reopened later; the indexes themselves are rebuilt by scanning the heap
// at startup (a deliberately simple choice we call out in the README).
// ---------------------------------------------------------------------------
#include "../common.h"
#include "../storage/heap_file.h"
#include "../index/bplus_tree.h"
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>

namespace minidb {

struct TableInfo {
    std::string                 name;
    Schema                      schema;
    int                         first_page_id = INVALID_PAGE_ID;
    int                         pk_index = -1; // schema position of PK, or -1
    std::unique_ptr<HeapFile>   heap;
    std::unique_ptr<BPlusTree>  index; // keyed on the PK column (INT only)
};

class Catalog {
public:
    Catalog(BufferPool* bp, std::string meta_file)
        : bp_(bp), meta_file_(std::move(meta_file)) {}

    bool exists(const std::string& name) const { return tables_.count(name) > 0; }

    TableInfo* get(const std::string& name) {
        auto it = tables_.find(name);
        return it == tables_.end() ? nullptr : it->second.get();
    }

    std::vector<std::string> table_names() const {
        std::vector<std::string> v;
        for (auto& [k, _] : tables_) v.push_back(k);
        return v;
    }

    TableInfo* create_table(const std::string& name, const Schema& schema) {
        if (exists(name)) throw std::runtime_error("table already exists: " + name);
        int first = HeapFile::create(bp_);
        auto info = make_table_info(name, schema, first);
        TableInfo* ptr = info.get();
        tables_[name] = std::move(info);
        persist();
        return ptr;
    }

    // Load table definitions from the metadata file and rebuild indexes.
    void load() {
        std::ifstream f(meta_file_);
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string name; int ncols, first;
            ss >> name >> ncols >> first;
            Schema schema;
            for (int i = 0; i < ncols; ++i) {
                std::string cname; int type, pk;
                ss >> cname >> type >> pk;
                schema.push_back(Column{cname, type == 0 ? Type::INT : Type::TEXT, pk == 1});
            }
            auto info = make_table_info(name, schema, first);
            rebuild_index(info.get());
            tables_[name] = std::move(info);
        }
    }

    void persist() {
        std::ofstream f(meta_file_, std::ios::trunc);
        for (auto& [name, info] : tables_) {
            f << name << " " << info->schema.size() << " " << info->first_page_id;
            for (auto& c : info->schema)
                f << " " << c.name << " " << (c.type == Type::INT ? 0 : 1) << " " << (c.is_primary_key ? 1 : 0);
            f << "\n";
        }
    }

    // Rebuild a table's B+ tree from the live tuples currently in its heap.
    void rebuild_index(TableInfo* info) {
        if (info->pk_index < 0) return;
        info->index = std::make_unique<BPlusTree>(8);
        for (auto& t : info->heap->scan()) {
            if (t.xmax != INVALID_TX) continue; // skip dead versions
            info->index->insert(as_int(t.row[info->pk_index]), t.rid);
        }
    }

private:
    std::unique_ptr<TableInfo> make_table_info(const std::string& name, const Schema& schema, int first) {
        auto info = std::make_unique<TableInfo>();
        info->name = name;
        info->schema = schema;
        info->first_page_id = first;
        info->pk_index = -1;
        for (int i = 0; i < (int)schema.size(); ++i)
            if (schema[i].is_primary_key && schema[i].type == Type::INT) info->pk_index = i;
        info->heap = std::make_unique<HeapFile>(bp_, schema, first);
        if (info->pk_index >= 0) info->index = std::make_unique<BPlusTree>(8);
        return info;
    }

    BufferPool*  bp_;
    std::string  meta_file_;
    std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
};

} // namespace minidb
