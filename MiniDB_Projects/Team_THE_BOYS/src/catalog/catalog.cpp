#include "catalog/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace minidb {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

ValueType ParseType(const std::string& t) {
    return Lower(t) == "string" ? ValueType::STRING : ValueType::INT;
}

}  // namespace

int TableSchema::ColumnIndex(const std::string& col) const {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (Lower(columns[i].name) == Lower(col)) return static_cast<int>(i);
    }
    throw std::runtime_error("Column not found: " + col);
}

const ColumnDef* TableSchema::PrimaryKeyColumn() const {
    for (const auto& c : columns) {
        if (c.primary_key) return &c;
    }
    return nullptr;
}

bool TableSchema::IsColumnIndexed(const std::string& col) const {
    for (const auto& c : columns) {
        if (Lower(c.name) == Lower(col)) {
            return c.primary_key || c.indexed;
        }
    }
    return false;
}

bool Catalog::CreateTable(const TableSchema& schema) {
    std::string key = Lower(schema.name);
    if (tables_.count(key)) return false;
    tables_[key] = schema;
    return true;
}

std::optional<TableSchema> Catalog::GetTable(const std::string& name) const {
    std::string key = Lower(name);
    auto it = tables_.find(key);
    if (it == tables_.end()) return std::nullopt;
    return it->second;
}

void Catalog::RegisterHeapFile(const std::string& table, std::unique_ptr<HeapFile> heap) {
    heaps_[Lower(table)] = std::move(heap);
}

void Catalog::RegisterIndex(const std::string& table, const std::string& col,
                            std::unique_ptr<BPlusTree> index) {
    std::string key = Lower(table);
    if (col.empty()) {
        pk_indexes_[key] = std::move(index);
    } else {
        secondary_indexes_[key + ":" + Lower(col)] = std::move(index);
    }
}

HeapFile* Catalog::GetHeapFile(const std::string& table) const {
    auto it = heaps_.find(Lower(table));
    return it == heaps_.end() ? nullptr : it->second.get();
}

BPlusTree* Catalog::GetPrimaryIndex(const std::string& table) const {
    auto it = pk_indexes_.find(Lower(table));
    return it == pk_indexes_.end() ? nullptr : it->second.get();
}

BPlusTree* Catalog::GetSecondaryIndex(const std::string& table, const std::string& col) const {
    auto it = secondary_indexes_.find(Lower(table) + ":" + Lower(col));
    return it == secondary_indexes_.end() ? nullptr : it->second.get();
}

void Catalog::Save(const std::string& filepath) const {
    std::ofstream out(filepath);
    for (const auto& [name, schema] : tables_) {
        out << "TABLE " << schema.name << ' ' << schema.heap_first_page << ' '
            << schema.pk_index_root << '\n';
        for (const auto& col : schema.columns) {
            out << "COL " << col.name << ' ' << (col.type == ValueType::STRING ? "STRING" : "INT");
            if (col.primary_key) out << " PK";
            if (col.indexed) {
                out << " IDX";
                if (!col.primary_key) {
                    auto it = schema.secondary_indexes.find(col.name);
                    if (it != schema.secondary_indexes.end()) {
                        out << ' ' << it->second;
                    }
                }
            }
            out << '\n';
        }
        out << "END\n";
    }
}

bool Catalog::Load(const std::string& filepath, PageManager* pm, BufferPool* bp) {
    std::ifstream in(filepath);
    if (!in.is_open()) return false;

    auto init_btree_page = [&](int page_id) {
        Page* p = bp->FetchPage(page_id);
        std::vector<char> blank(PAGE_SIZE, '\0');
        blank[0] = 1;
        blank[1] = 0;
        int no_next = INVALID_PAGE_ID;
        std::memcpy(blank.data() + 8, &no_next, sizeof(no_next));
        std::memcpy(p->MutableData(), blank.data(), PAGE_SIZE);
        bp->UnpinPage(page_id, true);
    };

    auto resolve_page = [&](int page_id, bool btree) -> int {
        if (page_id >= 0 && page_id < pm->PageCount()) return page_id;
        int new_page = pm->AllocatePage();
        if (btree) {
            init_btree_page(new_page);
        } else {
            Page* p = bp->FetchPage(new_page);
            p->Initialize();
            bp->UnpinPage(new_page, true);
        }
        return new_page;
    };

    auto register_table = [&](TableSchema& schema) {
        schema.heap_first_page = resolve_page(schema.heap_first_page, false);
        schema.pk_index_root = resolve_page(schema.pk_index_root, true);
        for (auto& [col, root] : schema.secondary_indexes) {
            root = resolve_page(root, true);
        }
        CreateTable(schema);
        auto heap = std::make_unique<HeapFile>(pm, bp, schema.heap_first_page);
        heap->set_last_page_id(schema.heap_first_page);
        RegisterHeapFile(schema.name, std::move(heap));
        auto pk = std::make_unique<BPlusTree>(pm, bp, schema.pk_index_root);
        RegisterIndex(schema.name, "", std::move(pk));
        for (const auto& [col, root] : schema.secondary_indexes) {
            auto sec = std::make_unique<BPlusTree>(pm, bp, root);
            RegisterIndex(schema.name, col, std::move(sec));
        }
    };

    TableSchema current;
    bool in_table = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "TABLE") {
            if (in_table) register_table(current);
            current = TableSchema{};
            iss >> current.name >> current.heap_first_page >> current.pk_index_root;
            in_table = true;
        } else if (tag == "COL" && in_table) {
            ColumnDef col;
            std::string type_str;
            iss >> col.name >> type_str;
            col.type = ParseType(type_str);
            std::string token;
            while (iss >> token) {
                if (token == "PK") {
                    col.primary_key = true;
                } else if (token == "IDX") {
                    col.indexed = true;
                } else {
                    col.indexed = true;
                    current.secondary_indexes[col.name] = std::stoi(token);
                }
            }
            current.columns.push_back(col);
        } else if (tag == "END" && in_table) {
            register_table(current);
            in_table = false;
        }
    }
    return !tables_.empty();
}

}  // namespace minidb
