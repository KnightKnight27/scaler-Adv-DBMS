#include "catalog.hpp"

#include <stdexcept>
#include <utility>

Catalog::Catalog(std::string dir) : dir_(std::move(dir)) {}

Table& Catalog::create_table(const std::string& name, const Schema& schema, int pk_col) {
    if (tables_.count(name))
        throw std::runtime_error("table already exists: " + name);
    if (pk_col < 0 || pk_col >= static_cast<int>(schema.columns.size()) ||
        schema.columns[pk_col].type != ColumnType::INT)
        throw std::runtime_error("primary key must be an existing INT column");

    auto table = std::make_unique<Table>();
    table->name = name;
    table->schema = schema;
    table->pk_col = pk_col;

    // Build the storage stack bottom-up: disk -> pool -> heap.
    table->disk = std::make_unique<DiskManager>(dir_ + "/" + name + ".db");
    table->pool = std::make_unique<BufferPool>(*table->disk);
    table->heap = std::make_unique<HeapFile>(*table->pool, *table->disk);
    table->index = std::make_unique<BPlusTree>();

    // If the data file already held rows, rebuild the PK index from the heap.
    if (table->disk->num_pages() > 0) {
        for (auto& [rid, bytes] : table->heap->scan()) {
            std::vector<Value> row = schema.deserialize(bytes);
            int key = std::get<int>(row[pk_col]);
            table->index->insert(key, rid);
            ++table->row_count;
        }
    }

    Table& ref = *table;
    tables_.emplace(name, std::move(table));
    return ref;
}

Table* Catalog::get_table(const std::string& name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}
